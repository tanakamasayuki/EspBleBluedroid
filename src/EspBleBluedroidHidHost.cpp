// HID over GATT (HOGP) host: the side that consumes a HID device's reports.
//
// A HOGP host cannot assume a layout. The device publishes a Report Map, and every
// report characteristic shares UUID 0x2A4D, so the host has to read the descriptor,
// read each Report Reference to learn which report an attribute carries, and then
// cut the fields out of each notification the way the descriptor says. That is what
// this file does; the parsing itself is `EspBleHidReportMap.h`, shared with the
// device side and pinned by tests/unit/report_map.
//
// The structural difference from EspBle is discovery. This backend allows one
// central GATT operation per link at a time, so discovery cannot fire its reads in
// parallel: it is a state machine that issues the next operation from the previous
// one's result, all observed with add*Listener() so an application keeps its own
// on*() callbacks (peer/multi_listener). EspBle can afford a straight-line
// sequence; here the sequence lives in `step()`.
//
//   discoverServices  →  1812 present, and the whole attribute snapshot with it
//   read 2a4b         →  the Report Map, parsed once
//   read each 2908    →  which report each 0x2A4D attribute is
//   read 2a4a         →  HID Information (the country code)
//   read 2a19         →  Battery Level, when the device has one
//   subscribe each Input Report
//
// Only then is the link ready, and onDiscovered() fires with what the device turned
// out to be.

#include "EspBleBluedroid.h"

#include "EspBleHidReportMap.h"

#include <cstring>

namespace
{
constexpr const char *HidServiceUuid = "1812";
constexpr const char *ReportMapUuid = "2a4b";
constexpr const char *HidInformationUuid = "2a4a";
constexpr const char *ReportUuid = "2a4d";
constexpr const char *ReportReferenceUuid = "2908";
constexpr const char *BatteryServiceUuid = "180f";
constexpr const char *BatteryLevelUuid = "2a19";

// HID Information (0x2A4A) is [bcdHID lo, bcdHID hi, country code, flags].
constexpr size_t HidInformationCountryIndex = 2;

// A modifier usage lives in the report's modifier byte rather than in the key
// array or the NKRO bitmap, in both directions.
constexpr uint8_t FirstModifierUsage = 0xe0;
constexpr uint8_t LastModifierUsage = 0xe7;
// The HID rollover code, which a boot keyboard sends instead of a key list when
// more keys are held than the report can carry. It is not a usage.
constexpr uint8_t RolloverUsage = 0x01;

// HID usage pages and the pointer usages a mouse report is made of. A host has to
// go by these rather than by field order: the descriptor decides the layout.
constexpr uint16_t GenericDesktopUsagePage = 0x01;
constexpr uint16_t ButtonUsagePage = 0x09;
constexpr uint16_t UsageX = 0x30;
constexpr uint16_t UsageY = 0x31;
constexpr uint16_t UsageWheel = 0x38;

bool uuidEquals(const String &value, const char *shortForm)
{
  // The client reports 128-bit forms; a 16-bit UUID appears as the Bluetooth base
  // UUID with the short form in it, so compare on the short form either way.
  if (value.equalsIgnoreCase(shortForm)) return true;
  if (value.length() != 36) return false;
  return value.substring(4, 8).equalsIgnoreCase(shortForm);
}
}  // namespace

// ---------------------------------------------------------------------------
// One discovered link's HID state, plus the discovery state machine.
// ---------------------------------------------------------------------------
struct EspBleHidKeyboardHostImpl
{
  static constexpr size_t MaxLinks = 2;
  static constexpr size_t MaxReports = 8;
  static constexpr size_t MaxReportMapLength = 512;
  static constexpr size_t MaxReportLength = 64;
  static constexpr size_t MaxFields = EspBleHidReportMapInfo::MaxFields;

  enum class Step : uint8_t
  {
    Idle = 0,
    Services,
    ReportMap,
    ReportReferences,
    HidInformation,
    BatteryLevel,
    Subscribe,
    Done,
  };

  // One 0x2A4D attribute of the peer, with what its Report Reference said.
  struct Report
  {
    uint16_t characteristicHandle = 0;
    uint16_t referenceHandle = 0;
    uint8_t reportId = 0;
    uint8_t reportType = 0;
    bool notifiable = false;
    bool subscribed = false;
  };

  struct Link
  {
    bool used = false;
    EspBleConnectionId connectionId = 0;
    Step step = Step::Idle;
    bool ready = false;
    size_t cursor = 0;  // which report reference / subscription is in flight

    Report reports[MaxReports];
    size_t reportCount = 0;

    uint8_t reportMap[MaxReportMapLength] = {};
    size_t reportMapLength = 0;
    EspBleHidReportMapInfo map;

    uint16_t outputHandle = 0;   // the keyboard Output Report, for the LEDs
    uint16_t vendorOutputHandle = 0;
    uint16_t vendorFeatureHandle = 0;
    bool hasBatteryLevel = false;
    uint8_t batteryLevel = 0;
    bool hasCountryCode = false;
    uint8_t countryCode = 0;

    // The keyboard state this host reports, kept per link so two devices do not
    // share one keyboard.
    EspBleHidKeyboardState keyboard;
    uint8_t mouseButtons = 0;
    uint16_t consumerUsage = 0;
    uint8_t systemUsage = 0;
    EspBleHidFieldValue fields[MaxFields];
  };

  explicit EspBleHidKeyboardHostImpl(EspBleBluedroid *owner) : owner(owner) {}

  EspBleBluedroid *owner;
  EspBleHidHost *api = nullptr;
  Link links[MaxLinks];
  bool listenersInstalled = false;
  bool autoRediscover = false;
  String rediscoverPeers[EspBleHidHost::MaxRediscoverPeers];
  size_t rediscoverPeerCount = 0;
  size_t droppedEvents = 0;
  size_t invalidReports = 0;
  EspBleKeyboardLayout layout = EspBleKeyboardLayout::EnUs;

  // One multi-observer list per event, the same shape the rest of the library
  // uses: on*() sets the primary, add*Listener() appends (peer/multi_listener).
  EspBleCallbackList<EspBleHidHost::DiscoveryCallback> discoveredCallbacks;
  EspBleCallbackList<EspBleHidHost::StateCallback> stateCallbacks;
  EspBleCallbackList<EspBleHidHost::KeyboardCallback> keyboardCallbacks;
  EspBleCallbackList<EspBleHidHost::MouseCallback> mouseCallbacks;
  EspBleCallbackList<EspBleHidHost::ConsumerControlCallback> consumerCallbacks;
  EspBleCallbackList<EspBleHidHost::SystemControlCallback> systemCallbacks;
  EspBleCallbackList<EspBleHidHost::GamepadCallback> gamepadCallbacks;
  EspBleCallbackList<EspBleHidHost::VendorInputCallback> vendorCallbacks;
  EspBleListenerId nextListenerId = 1;

  // Dispatch in registration order: the primary first, then the listeners. The
  // snapshot is taken first so a callback may add or remove one.
  template <typename Callback, typename Argument>
  static void dispatch(
    const EspBleCallbackList<Callback> &list, const Argument &argument)
  {
    std::shared_ptr<Callback> callbacks[EspBleCallbackList<Callback>::Capacity];
    const size_t count = list.snapshot(callbacks);
    for (size_t index = 0; index < count; ++index) (*callbacks[index])(argument);
  }

  template <typename Callback>
  EspBleListenerId addListener(
    EspBleCallbackList<Callback> &list, Callback callback)
  {
    const EspBleListenerId id = nextListenerId;
    if (list.add(std::move(callback), id) == EspBleInvalidListenerId)
    {
      return EspBleInvalidListenerId;
    }
    ++nextListenerId;
    return id;
  }

  Link *linkFor(EspBleConnectionId connectionId, bool create)
  {
    for (Link &link : links)
    {
      if (link.used && link.connectionId == connectionId) return &link;
    }
    if (!create) return nullptr;
    for (Link &link : links)
    {
      if (!link.used)
      {
        link = Link();
        link.used = true;
        link.connectionId = connectionId;
        return &link;
      }
    }
    return nullptr;
  }

  Link *linkInStep(EspBleConnectionId connectionId, Step step)
  {
    Link *link = linkFor(connectionId, false);
    if (link == nullptr || link->step != step) return nullptr;
    return link;
  }

  void fail(Link &link, EspBleError error, const char *detail)
  {
    EspBleHidKeyboardHostDiscovery result;
    result.connectionId = link.connectionId;
    result.success = false;
    result.error = error;
    result.detail = detail;
    link.step = Step::Idle;
    link.ready = false;
    dispatch(discoveredCallbacks, result);
  }

  void finish(Link &link)
  {
    link.step = Step::Done;
    link.ready = true;
    // Remember the peer, so setAutoRediscover() has something to recognise.
    EspBleConnection connection;
    if (api != nullptr && owner->connection(link.connectionId, connection))
    {
      api->rememberRediscoverPeer(connection.peerAddress);
    }
    EspBleHidKeyboardHostDiscovery result;
    result.connectionId = link.connectionId;
    result.success = true;
    // The keyboard's report ID, which is what a keyboard host needs to name the
    // reports it will receive; 0 when the device has no keyboard.
    for (size_t index = 0; index < link.reportCount; ++index)
    {
      if (link.reports[index].reportType != ESP_BLE_HID_REPORT_TYPE_INPUT) continue;
      const EspBleHidReportMapEntry *entry =
        entryFor(link, link.reports[index].reportId);
      if (entry != nullptr && entry->kind == EspBleHidReportKind::Keyboard)
      {
        result.reportId = link.reports[index].reportId;
        break;
      }
    }
    result.hasCountryCode = link.hasCountryCode;
    result.countryCode = link.countryCode;
    result.hasOutputReport = link.outputHandle != 0;
    result.hasBatteryLevel = link.hasBatteryLevel;
    result.batteryLevel = link.batteryLevel;
    dispatch(discoveredCallbacks, result);
  }

  const EspBleHidReportMapEntry *entryFor(const Link &link, uint8_t reportId) const
  {
    for (size_t index = 0; index < link.map.count; ++index)
    {
      if (link.map.entries[index].reportId == reportId)
      {
        return &link.map.entries[index];
      }
    }
    return nullptr;
  }

  // Issue the operation the current step needs. One at a time: this backend
  // rejects a second central GATT operation while one is in flight.
  void step(Link &link)
  {
    switch (link.step)
    {
      case Step::Services:
        if (!owner->discoverServices(link.connectionId))
        {
          fail(link, EspBleError::BackendFailure, "failed to start service discovery");
        }
        return;
      case Step::ReportMap:
        if (!owner->readCharacteristic(
              link.connectionId, HidServiceUuid, ReportMapUuid))
        {
          fail(link, EspBleError::BackendFailure, "failed to read the Report Map");
        }
        return;
      case Step::ReportReferences:
        if (link.cursor >= link.reportCount)
        {
          link.step = Step::HidInformation;
          step(link);
          return;
        }
        if (!owner->readDescriptor(
              link.connectionId, link.reports[link.cursor].referenceHandle))
        {
          fail(link, EspBleError::BackendFailure,
            "failed to read a Report Reference");
        }
        return;
      case Step::HidInformation:
        if (!owner->readCharacteristic(
              link.connectionId, HidServiceUuid, HidInformationUuid))
        {
          // Optional: a device may omit it, and a host still works.
          link.step = Step::BatteryLevel;
          step(link);
        }
        return;
      case Step::BatteryLevel:
        if (!owner->readCharacteristic(
              link.connectionId, BatteryServiceUuid, BatteryLevelUuid))
        {
          link.step = Step::Subscribe;
          link.cursor = 0;
          step(link);
        }
        return;
      case Step::Subscribe:
        while (link.cursor < link.reportCount &&
               (link.reports[link.cursor].reportType !=
                  ESP_BLE_HID_REPORT_TYPE_INPUT ||
                 !link.reports[link.cursor].notifiable))
        {
          ++link.cursor;
        }
        if (link.cursor >= link.reportCount)
        {
          finish(link);
          return;
        }
        if (!owner->subscribe(
              link.connectionId, link.reports[link.cursor].characteristicHandle))
        {
          fail(link, EspBleError::BackendFailure,
            "failed to subscribe to an Input Report");
        }
        return;
      default:
        return;
    }
  }

  // Collect the HID service's characteristics from the discovery snapshot: every
  // 0x2A4D with the Report Reference descriptor that belongs to it, plus the
  // keyboard Output Report and the vendor writable reports.
  bool collectReports(Link &link)
  {
    link.reportCount = 0;
    link.outputHandle = 0;
    const size_t count =
      owner->discoveredCharacteristicCount(link.connectionId, HidServiceUuid);
    for (size_t index = 0; index < count; ++index)
    {
      EspBleGattCharacteristicInfo characteristic;
      if (!owner->discoveredCharacteristic(
            link.connectionId, index, characteristic, HidServiceUuid))
      {
        continue;
      }
      if (!uuidEquals(characteristic.characteristicUuid, ReportUuid)) continue;
      if (link.reportCount >= MaxReports) break;
      Report &report = link.reports[link.reportCount];
      report = Report();
      report.characteristicHandle = characteristic.handle;
      report.notifiable = characteristic.notifiable;
      // The Report Reference of *this* characteristic, found by the handle it
      // belongs to: every one of them is 0x2908 under a 0x2A4D, so the UUID pair
      // cannot name it.
      const size_t descriptorCount =
        owner->discoveredDescriptorCount(link.connectionId, HidServiceUuid);
      for (size_t descriptorIndex = 0; descriptorIndex < descriptorCount;
           ++descriptorIndex)
      {
        EspBleGattDescriptorInfo descriptor;
        if (!owner->discoveredDescriptor(
              link.connectionId, descriptorIndex, descriptor, HidServiceUuid))
        {
          continue;
        }
        if (descriptor.characteristicHandle != characteristic.handle) continue;
        if (!uuidEquals(descriptor.descriptorUuid, ReportReferenceUuid)) continue;
        report.referenceHandle = descriptor.handle;
        break;
      }
      if (report.referenceHandle == 0) continue;  // not a report after all
      ++link.reportCount;
    }
    return link.reportCount > 0;
  }

  // --- report decoding -----------------------------------------------------

  void dispatchKeyboard(Link &link, const Report &report, const uint8_t *data,
    size_t length, const EspBleHidReportMapEntry &entry)
  {
    EspBleHidKeyboardState previous = link.keyboard;
    EspBleHidKeyboardState &state = link.keyboard;
    state.connectionId = link.connectionId;
    state.reportId = report.reportId;
    memset(state.bitmap, 0, sizeof(state.bitmap));
    memset(state.changedBitmap, 0, sizeof(state.changedBitmap));
    state.modifiers = length > 0 ? data[0] : 0;

    if (entry.keyboardHasModifiers)
    {
      // Where the modifier byte is comes from the descriptor, not from an assumed
      // offset 0 — the parser found it while walking the report.
      const size_t modifierByte = entry.keyboardModifierBitOffset / 8;
      state.modifiers = modifierByte < length ? data[modifierByte] : 0;
    }
    if (entry.keyboardBitmap)
    {
      // An NKRO keyboard: a run of single-bit fields, one per usage, starting at
      // the usage the descriptor declared as its minimum.
      for (uint16_t bit = 0; bit < entry.keyboardBitmapBitCount; ++bit)
      {
        const size_t offset = entry.keyboardBitmapBitOffset + bit;
        if (offset / 8 >= length) break;
        if ((data[offset / 8] & static_cast<uint8_t>(1u << (offset % 8))) == 0)
        {
          continue;
        }
        const uint16_t usage =
          static_cast<uint16_t>(entry.keyboardBitmapUsageMinimum + bit);
        if (usage >= 8 * EspBleHidKeyboardState::BitmapSize) continue;
        state.bitmap[usage >> 3] |= static_cast<uint8_t>(1u << (usage & 7));
      }
    }
    else
    {
      // The boot-compatible layout: [modifiers, reserved, keycode1..6]. All six
      // slots holding 0x01 is the rollover code, not six keys.
      size_t heldRollover = 0;
      // The key array starts after the modifier byte and the reserved byte.
      const size_t firstKey =
        entry.keyboardHasModifiers ? entry.keyboardModifierBitOffset / 8 + 2 : 0;
      for (size_t index = firstKey; index < length; ++index)
      {
        const uint8_t usage = data[index];
        if (usage == 0) continue;
        if (usage == RolloverUsage) ++heldRollover;
        state.bitmap[usage >> 3] |= static_cast<uint8_t>(1u << (usage & 7));
      }
      if (heldRollover > 1)
      {
        // Too many keys were held for the device to name them, so the previous
        // state stands: reporting 0x01 as a pressed usage would be a lie.
        ++invalidReports;
        link.keyboard = previous;
        return;
      }
    }
    // The modifier usages live in the modifier byte, and isDown() answers for them
    // too, so mirror them into the bitmap.
    for (uint8_t usage = FirstModifierUsage; usage <= LastModifierUsage; ++usage)
    {
      if ((state.modifiers & static_cast<uint8_t>(1u << (usage - FirstModifierUsage)))
            != 0)
      {
        state.bitmap[usage >> 3] |= static_cast<uint8_t>(1u << (usage & 7));
      }
    }
    state.numLock = previous.numLock;
    state.capsLock = previous.capsLock;
    state.scrollLock = previous.scrollLock;
    state.compose = previous.compose;
    state.kana = previous.kana;
    for (size_t index = 0; index < EspBleHidKeyboardState::BitmapSize; ++index)
    {
      state.changedBitmap[index] =
        static_cast<uint8_t>(state.bitmap[index] ^ previous.bitmap[index]);
    }

    dispatch(stateCallbacks, state);
    // One event per changed usage, which is what a sketch reacts to.
    for (uint16_t usage = 0; usage < 8 * EspBleHidKeyboardState::BitmapSize; ++usage)
    {
      const uint8_t key = static_cast<uint8_t>(usage);
      if (!state.wasPressed(key) && !state.wasReleased(key)) continue;
      EspBleHidKeyboardEvent event;
      event.connectionId = link.connectionId;
      event.reportId = report.reportId;
      event.rawData = data;
      event.rawLength = length;
      event.usage = key;
      event.modifiers = state.modifiers;
      event.pressed = state.isDown(key);
      event.released = !event.pressed;
      event.numLock = state.numLock;
      event.capsLock = state.capsLock;
      event.scrollLock = state.scrollLock;
      event.compose = state.compose;
      event.kana = state.kana;
      event.unicode = espBleUsageToUnicode(key, state.modifiers, layout,
        state.capsLock, state.numLock);
      event.ascii = event.unicode <= 0xff ? static_cast<uint8_t>(event.unicode) : 0;
      dispatch(keyboardCallbacks, event);
    }
  }

  void dispatchMouse(Link &link, const Report &report, const uint8_t *data,
    size_t length, const EspBleHidReportMapEntry &entry)
  {
    EspBleHidMouseEvent event;
    event.connectionId = link.connectionId;
    event.reportId = report.reportId;
    event.rawData = data;
    event.rawLength = length;
    event.previousButtons = link.mouseButtons;
    // The field offsets come from the device's descriptor, not from an assumed
    // [buttons, x, y, wheel]: a mouse may use 16-bit axes or a longer button field.
    // Which field is which is the usage the descriptor gave it.
    uint8_t buttonBit = 0;
    for (size_t index = 0; index < link.map.fieldCount; ++index)
    {
      const EspBleHidReportField &field = link.map.fields[index];
      if (field.reportId != report.reportId) continue;
      const int32_t value = espBleHidReadFieldValue(field, data, length);
      if (field.usagePage == ButtonUsagePage)
      {
        // Buttons may be one multi-bit field or one field per button, depending on
        // how the descriptor declared them; both end up in the same bit mask.
        if (field.bitSize == 1)
        {
          if (value != 0)
          {
            event.buttons |= static_cast<uint8_t>(1u << buttonBit);
          }
          ++buttonBit;
        }
        else
        {
          event.buttons = static_cast<uint8_t>(value);
        }
        continue;
      }
      if (field.usagePage != GenericDesktopUsagePage) continue;
      if (field.usage == UsageX) event.x = static_cast<int16_t>(value);
      else if (field.usage == UsageY) event.y = static_cast<int16_t>(value);
      else if (field.usage == UsageWheel) event.wheel = static_cast<int16_t>(value);
    }
    (void)entry;
    event.moved = event.x != 0 || event.y != 0 || event.wheel != 0;
    event.buttonsChanged = event.buttons != event.previousButtons;
    link.mouseButtons = event.buttons;
    dispatch(mouseCallbacks, event);
  }

  void dispatchConsumer(Link &link, const Report &report, const uint8_t *data,
    size_t length)
  {
    EspBleHidConsumerControlEvent event;
    event.connectionId = link.connectionId;
    event.reportId = report.reportId;
    event.rawData = data;
    event.rawLength = length;
    // One 16-bit usage per report, little-endian, and 0 is the release.
    event.usage = length >= 2
      ? static_cast<uint16_t>(data[0] | (static_cast<uint16_t>(data[1]) << 8))
      : (length == 1 ? data[0] : 0);
    event.pressed = event.usage != 0;
    event.released = event.usage == 0 && link.consumerUsage != 0;
    if (event.released) event.usage = link.consumerUsage;
    link.consumerUsage = event.pressed ? event.usage : 0;
    dispatch(consumerCallbacks, event);
  }

  void dispatchSystem(Link &link, const Report &report, const uint8_t *data,
    size_t length)
  {
    EspBleHidSystemControlEvent event;
    event.connectionId = link.connectionId;
    event.reportId = report.reportId;
    event.rawData = data;
    event.rawLength = length;
    event.usage = length >= 1 ? data[0] : 0;
    event.pressed = event.usage != 0;
    event.released = event.usage == 0 && link.systemUsage != 0;
    if (event.released) event.usage = link.systemUsage;
    link.systemUsage = event.pressed ? event.usage : 0;
    dispatch(systemCallbacks, event);
  }

  void dispatchGamepad(Link &link, const Report &report, const uint8_t *data,
    size_t length)
  {
    // A gamepad has no fixed layout worth assuming, so every field the descriptor
    // declared is handed over with its usage and range.
    size_t count = 0;
    for (size_t index = 0; index < link.map.fieldCount && count < MaxFields; ++index)
    {
      const EspBleHidReportField &field = link.map.fields[index];
      if (field.reportId != report.reportId) continue;
      EspBleHidFieldValue &value = link.fields[count++];
      value.reportId = field.reportId;
      value.usagePage = field.usagePage;
      value.usage = field.usage;
      value.value = espBleHidReadFieldValue(field, data, length);
      value.logicalMin = field.logicalMin;
      value.logicalMax = field.logicalMax;
      value.bitOffset = field.bitOffset;
      value.bitSize = field.bitSize;
      value.flags = field.flags;
    }
    EspBleHidGamepadEvent event;
    event.connectionId = link.connectionId;
    event.reportId = report.reportId;
    event.rawData = data;
    event.rawLength = length;
    event.fields = link.fields;
    event.fieldCount = count;
    event.changed = count > 0;
    dispatch(gamepadCallbacks, event);
  }

  void dispatchVendor(Link &link, const Report &report, const uint8_t *data,
    size_t length)
  {
    EspBleHidVendorInputEvent event;
    event.connectionId = link.connectionId;
    event.reportId = report.reportId;
    event.rawData = data;
    event.rawLength = length;
    dispatch(vendorCallbacks, event);
  }

  void handleNotification(const EspBleGattNotification &notification)
  {
    Link *link = linkFor(notification.connectionId, false);
    if (link == nullptr || !link->ready) return;
    const Report *report = nullptr;
    for (size_t index = 0; index < link->reportCount; ++index)
    {
      if (link->reports[index].characteristicHandle == notification.handle)
      {
        report = &link->reports[index];
        break;
      }
    }
    if (report == nullptr) return;
    const uint8_t *data =
      reinterpret_cast<const uint8_t *>(notification.value.c_str());
    const size_t length = notification.value.length();
    if (length == 0 || length > MaxReportLength)
    {
      ++invalidReports;
      return;
    }
    const EspBleHidReportMapEntry *entry = entryFor(*link, report->reportId);
    if (entry == nullptr)
    {
      // A report the descriptor never declared: the device and its Report Map
      // disagree, and guessing a layout would be worse than counting it.
      ++invalidReports;
      return;
    }
    switch (entry->kind)
    {
      case EspBleHidReportKind::Keyboard:
        dispatchKeyboard(*link, *report, data, length, *entry);
        return;
      case EspBleHidReportKind::Mouse:
        dispatchMouse(*link, *report, data, length, *entry);
        return;
      case EspBleHidReportKind::ConsumerControl:
        dispatchConsumer(*link, *report, data, length);
        return;
      case EspBleHidReportKind::SystemControl:
        dispatchSystem(*link, *report, data, length);
        return;
      case EspBleHidReportKind::Gamepad:
        dispatchGamepad(*link, *report, data, length);
        return;
      default:
        dispatchVendor(*link, *report, data, length);
        return;
    }
  }

  // --- discovery results ---------------------------------------------------

  void handleServicesDiscovered(const EspBleGattResult &result)
  {
    Link *link = linkInStep(result.connectionId, Step::Services);
    if (link == nullptr) return;
    if (!result.success)
    {
      fail(*link, result.error, result.detail.c_str());
      return;
    }
    bool hasHid = false;
    const size_t count = owner->discoveredServiceCount(link->connectionId);
    for (size_t index = 0; index < count; ++index)
    {
      EspBleGattServiceInfo service;
      if (!owner->discoveredService(link->connectionId, index, service)) continue;
      if (uuidEquals(service.serviceUuid, HidServiceUuid)) hasHid = true;
    }
    if (!hasHid)
    {
      fail(*link, EspBleError::NotFound, "the peer has no HID service");
      return;
    }
    // One discovery fills the whole snapshot — services, characteristics and
    // descriptors — so the reports can be collected from it right away.
    if (!collectReports(*link))
    {
      fail(*link, EspBleError::NotFound,
        "the HID service publishes no Report characteristic");
      return;
    }
    link->step = Step::ReportMap;
    step(*link);
  }

  void handleCharacteristicRead(const EspBleGattResult &result)
  {
    Link *link = linkFor(result.connectionId, false);
    if (link == nullptr) return;
    if (link->step == Step::ReportMap)
    {
      if (!result.success || result.value.length() == 0)
      {
        fail(*link, result.success ? EspBleError::NotFound : result.error,
          result.success ? "the Report Map is empty" : result.detail.c_str());
        return;
      }
      link->reportMapLength = result.value.length() > MaxReportMapLength
        ? MaxReportMapLength : result.value.length();
      memcpy(link->reportMap, result.value.c_str(), link->reportMapLength);
      link->map = espBleParseHidReportMap(link->reportMap, link->reportMapLength);
      if (link->map.count == 0)
      {
        fail(*link, EspBleError::Unsupported,
          "the Report Map declares no report this host understands");
        return;
      }
      link->step = Step::ReportReferences;
      link->cursor = 0;
      step(*link);
      return;
    }
    if (link->step == Step::HidInformation)
    {
      if (result.success && result.value.length() > HidInformationCountryIndex)
      {
        link->hasCountryCode = true;
        link->countryCode =
          static_cast<uint8_t>(result.value[HidInformationCountryIndex]);
      }
      link->step = Step::BatteryLevel;
      step(*link);
      return;
    }
    if (link->step == Step::BatteryLevel)
    {
      if (result.success && result.value.length() == 1)
      {
        link->hasBatteryLevel = true;
        link->batteryLevel = static_cast<uint8_t>(result.value[0]);
      }
      link->step = Step::Subscribe;
      link->cursor = 0;
      step(*link);
      return;
    }
  }

  void handleDescriptorRead(const EspBleGattResult &result)
  {
    Link *link = linkInStep(result.connectionId, Step::ReportReferences);
    if (link == nullptr || link->cursor >= link->reportCount) return;
    Report &report = link->reports[link->cursor];
    if (result.success && result.value.length() == 2)
    {
      report.reportId = static_cast<uint8_t>(result.value[0]);
      report.reportType = static_cast<uint8_t>(result.value[1]);
      // The writable reports a host may use: the keyboard LEDs, and the vendor
      // profile's Output and Feature reports.
      const EspBleHidReportMapEntry *entry = entryFor(*link, report.reportId);
      const bool keyboard =
        entry != nullptr && entry->kind == EspBleHidReportKind::Keyboard;
      if (report.reportType == ESP_BLE_HID_REPORT_TYPE_OUTPUT)
      {
        if (keyboard) link->outputHandle = report.characteristicHandle;
        else link->vendorOutputHandle = report.characteristicHandle;
      }
      else if (report.reportType == ESP_BLE_HID_REPORT_TYPE_FEATURE)
      {
        link->vendorFeatureHandle = report.characteristicHandle;
      }
    }
    ++link->cursor;
    step(*link);
  }

  void handleSubscribed(const EspBleGattResult &result)
  {
    Link *link = linkInStep(result.connectionId, Step::Subscribe);
    if (link == nullptr || link->cursor >= link->reportCount) return;
    if (!result.success)
    {
      fail(*link, result.error, result.detail.c_str());
      return;
    }
    link->reports[link->cursor].subscribed = true;
    ++link->cursor;
    step(*link);
  }

  void installListeners()
  {
    if (listenersInstalled) return;
    listenersInstalled = true;
    EspBleHidKeyboardHostImpl *self = this;
    // Observers rather than the primary on*() callbacks, so a sketch keeps its own
    // GATT client callbacks while the host drives its sequence.
    owner->addServicesDiscoveredListener(
      [self](const EspBleGattResult &result) { self->handleServicesDiscovered(result); });
    owner->addCharacteristicReadListener(
      [self](const EspBleGattResult &result) { self->handleCharacteristicRead(result); });
    owner->addDescriptorReadListener(
      [self](const EspBleGattResult &result) { self->handleDescriptorRead(result); });
    owner->addSubscribedListener(
      [self](const EspBleGattResult &result) { self->handleSubscribed(result); });
    owner->addNotificationListener(
      [self](const EspBleGattNotification &notification) {
        self->handleNotification(notification);
      });
    // A link that is gone takes its handles with it, and a re-encrypted known peer
    // is the trigger for the optional automatic rediscovery.
    owner->addDisconnectedListener([self](const EspBleConnection &connection) {
      if (self->api != nullptr) self->api->handleDisconnected(connection.id);
    });
    owner->addSecurityChangedListener([self](const EspBleSecurityChanged &event) {
      if (self->api != nullptr) self->api->handleSecurityEstablished(event);
    });
  }

  bool writeReport(EspBleConnectionId connectionId, uint16_t handle,
    const uint8_t *data, size_t length, bool response)
  {
    if (handle == 0)
    {
      owner->setError(EspBleError::NotFound,
        "the peer has no such writable HID report");
      return false;
    }
    return owner->writeCharacteristic(connectionId, handle, data, length, response);
  }
};

// ---------------------------------------------------------------------------
// EspBleHidHost
// ---------------------------------------------------------------------------

EspBleHidHost::EspBleHidHost(EspBleBluedroid *owner) : owner_(owner) {}

EspBleHidHost::~EspBleHidHost()
{
  delete impl_;
  impl_ = nullptr;
}

bool EspBleHidHost::discover(EspBleConnectionId connectionId)
{
  if (!owner_->initialized())
  {
    owner_->setError(EspBleError::InvalidState, "call begin() before discover()");
    return false;
  }
  EspBleConnection connection;
  if (!owner_->connection(connectionId, connection))
  {
    owner_->setError(EspBleError::NotFound, "connection ID was not found");
    return false;
  }
  if (impl_ == nullptr)
  {
    impl_ = new EspBleHidKeyboardHostImpl(owner_);
    if (impl_ == nullptr)
    {
      owner_->setError(EspBleError::ResourceExhausted,
        "failed to allocate HID Host state");
      return false;
    }
    impl_->api = this;
  }
  impl_->installListeners();
  EspBleHidKeyboardHostImpl::Link *link = impl_->linkFor(connectionId, true);
  if (link == nullptr)
  {
    owner_->setError(EspBleError::ResourceExhausted, "too many HID Host links");
    return false;
  }
  if (link->step != EspBleHidKeyboardHostImpl::Step::Idle &&
      link->step != EspBleHidKeyboardHostImpl::Step::Done)
  {
    owner_->setError(EspBleError::InvalidState,
      "HID discovery is already running on this connection");
    return false;
  }
  // Start from a clean slate: a rediscovery must not keep the previous handles.
  const EspBleConnectionId id = link->connectionId;
  *link = EspBleHidKeyboardHostImpl::Link();
  link->used = true;
  link->connectionId = id;
  link->step = EspBleHidKeyboardHostImpl::Step::Services;
  impl_->step(*link);
  if (link->step == EspBleHidKeyboardHostImpl::Step::Idle) return false;
  owner_->clearError();
  return true;
}

bool EspBleHidHost::setKeyboardLeds(EspBleConnectionId connectionId, bool numLock,
  bool capsLock, bool scrollLock, bool compose, bool kana)
{
  if (impl_ == nullptr)
  {
    owner_->setError(EspBleError::InvalidState, "call discover() first");
    return false;
  }
  EspBleHidKeyboardHostImpl::Link *link = impl_->linkFor(connectionId, false);
  if (link == nullptr || !link->ready)
  {
    owner_->setError(EspBleError::InvalidState, "HID discovery has not finished");
    return false;
  }
  const uint8_t leds = static_cast<uint8_t>(
    (numLock ? 0x01 : 0) | (capsLock ? 0x02 : 0) | (scrollLock ? 0x04 : 0) |
    (compose ? 0x08 : 0) | (kana ? 0x10 : 0));
  if (!impl_->writeReport(connectionId, link->outputHandle, &leds, 1, false))
  {
    return false;
  }
  // The LED flags belong to the state this host reports, so a later keyboard event
  // carries what the host asked for rather than what it last saw.
  link->keyboard.numLock = numLock;
  link->keyboard.capsLock = capsLock;
  link->keyboard.scrollLock = scrollLock;
  link->keyboard.compose = compose;
  link->keyboard.kana = kana;
  return true;
}

bool EspBleHidHost::sendVendorOutput(
  EspBleConnectionId connectionId, const uint8_t *data, size_t length)
{
  if (impl_ == nullptr || data == nullptr || length == 0)
  {
    owner_->setError(EspBleError::InvalidArgument, "invalid vendor output report");
    return false;
  }
  EspBleHidKeyboardHostImpl::Link *link = impl_->linkFor(connectionId, false);
  if (link == nullptr || !link->ready)
  {
    owner_->setError(EspBleError::InvalidState, "HID discovery has not finished");
    return false;
  }
  return impl_->writeReport(
    connectionId, link->vendorOutputHandle, data, length, false);
}

bool EspBleHidHost::sendVendorFeature(
  EspBleConnectionId connectionId, const uint8_t *data, size_t length)
{
  if (impl_ == nullptr || data == nullptr || length == 0)
  {
    owner_->setError(EspBleError::InvalidArgument, "invalid vendor feature report");
    return false;
  }
  EspBleHidKeyboardHostImpl::Link *link = impl_->linkFor(connectionId, false);
  if (link == nullptr || !link->ready)
  {
    owner_->setError(EspBleError::InvalidState, "HID discovery has not finished");
    return false;
  }
  // A Feature report is configuration, so it is always written with a response.
  return impl_->writeReport(
    connectionId, link->vendorFeatureHandle, data, length, true);
}

bool EspBleHidHost::ready(EspBleConnectionId connectionId) const
{
  if (impl_ == nullptr) return false;
  const EspBleHidKeyboardHostImpl::Link *link =
    const_cast<EspBleHidKeyboardHostImpl *>(impl_)->linkFor(connectionId, false);
  return link != nullptr && link->ready;
}

size_t EspBleHidHost::droppedEventCount() const
{
  return impl_ == nullptr ? 0 : impl_->droppedEvents;
}

size_t EspBleHidHost::invalidInputReportCount() const
{
  return impl_ == nullptr ? 0 : impl_->invalidReports;
}

void EspBleHidHost::setKeyboardLayout(EspBleKeyboardLayout layout)
{
  if (impl_ != nullptr) impl_->layout = layout;
  layout_ = layout;
}

EspBleKeyboardLayout EspBleHidHost::keyboardLayout() const { return layout_; }

void EspBleHidHost::setAutoRediscover(bool enable)
{
  autoRediscover_ = enable;
  if (impl_ != nullptr) impl_->autoRediscover = enable;
}

bool EspBleHidHost::autoRediscover() const { return autoRediscover_; }

void EspBleHidHost::resetBackend()
{
  if (impl_ == nullptr) return;
  for (auto &link : impl_->links) link = EspBleHidKeyboardHostImpl::Link();
}

void EspBleHidHost::handleDisconnected(EspBleConnectionId connectionId)
{
  if (impl_ == nullptr) return;
  EspBleHidKeyboardHostImpl::Link *link = impl_->linkFor(connectionId, false);
  if (link != nullptr) *link = EspBleHidKeyboardHostImpl::Link();
}

void EspBleHidHost::handleSecurityEstablished(const EspBleSecurityChanged &event)
{
  if (impl_ == nullptr || !autoRediscover_ || !event.success ||
      !event.connection.encrypted)
  {
    return;
  }
  // Only a peer this host discovered before: an automatic discovery of an unknown
  // device would be a surprise, and the point is resuming a known one.
  bool known = false;
  for (size_t index = 0; index < impl_->rediscoverPeerCount; ++index)
  {
    if (impl_->rediscoverPeers[index] == event.connection.peerAddress) known = true;
  }
  if (!known) return;
  discover(event.connection.id);
}

EspBleHidKeyboardHostImpl *EspBleHidHost::ensureImpl()
{
  if (impl_ == nullptr)
  {
    impl_ = new EspBleHidKeyboardHostImpl(owner_);
    if (impl_ == nullptr) return nullptr;
    impl_->api = this;
    impl_->layout = layout_;
    impl_->autoRediscover = autoRediscover_;
  }
  return impl_;
}

#define ESP_BLE_HID_HOST_EVENT(setter, adder, list, type)                      \
  void EspBleHidHost::setter(type callback)                                    \
  {                                                                            \
    EspBleHidKeyboardHostImpl *impl = ensureImpl();                            \
    if (impl != nullptr) impl->list.setPrimary(std::move(callback));           \
  }                                                                            \
  EspBleListenerId EspBleHidHost::adder(type callback)                         \
  {                                                                            \
    EspBleHidKeyboardHostImpl *impl = ensureImpl();                            \
    if (impl == nullptr) return EspBleInvalidListenerId;                       \
    return impl->addListener(impl->list, std::move(callback));                 \
  }

ESP_BLE_HID_HOST_EVENT(
  onDiscovered, addDiscoveredListener, discoveredCallbacks, DiscoveryCallback)
ESP_BLE_HID_HOST_EVENT(
  onKeyboardState, addKeyboardStateListener, stateCallbacks, StateCallback)
ESP_BLE_HID_HOST_EVENT(
  onKeyboard, addKeyboardListener, keyboardCallbacks, KeyboardCallback)
ESP_BLE_HID_HOST_EVENT(onMouse, addMouseListener, mouseCallbacks, MouseCallback)
ESP_BLE_HID_HOST_EVENT(onConsumerControl, addConsumerControlListener,
  consumerCallbacks, ConsumerControlCallback)
ESP_BLE_HID_HOST_EVENT(onSystemControl, addSystemControlListener, systemCallbacks,
  SystemControlCallback)
ESP_BLE_HID_HOST_EVENT(
  onGamepad, addGamepadListener, gamepadCallbacks, GamepadCallback)
ESP_BLE_HID_HOST_EVENT(
  onVendorInput, addVendorInputListener, vendorCallbacks, VendorInputCallback)

#undef ESP_BLE_HID_HOST_EVENT

bool EspBleHidHost::removeListener(EspBleListenerId listenerId)
{
  if (impl_ == nullptr || listenerId == EspBleInvalidListenerId) return false;
  // One id space across the host's events, so a caller only has to keep the id.
  return impl_->discoveredCallbacks.remove(listenerId) ||
    impl_->stateCallbacks.remove(listenerId) ||
    impl_->keyboardCallbacks.remove(listenerId) ||
    impl_->mouseCallbacks.remove(listenerId) ||
    impl_->consumerCallbacks.remove(listenerId) ||
    impl_->systemCallbacks.remove(listenerId) ||
    impl_->gamepadCallbacks.remove(listenerId) ||
    impl_->vendorCallbacks.remove(listenerId);
}

void EspBleHidHost::rememberRediscoverPeer(const String &address)
{
  if (impl_ == nullptr || address.length() == 0) return;
  for (size_t index = 0; index < impl_->rediscoverPeerCount; ++index)
  {
    if (impl_->rediscoverPeers[index] == address) return;
  }
  if (impl_->rediscoverPeerCount >= MaxRediscoverPeers)
  {
    // Keep the most recent peers: the oldest bond is the least likely to come back.
    for (size_t index = 1; index < MaxRediscoverPeers; ++index)
    {
      impl_->rediscoverPeers[index - 1] = impl_->rediscoverPeers[index];
    }
    impl_->rediscoverPeerCount = MaxRediscoverPeers - 1;
  }
  impl_->rediscoverPeers[impl_->rediscoverPeerCount++] = address;
}
