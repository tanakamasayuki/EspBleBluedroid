// HID over GATT (HOGP) device: the shared device manager and the keyboard
// profile.
//
// EspBle drives NimBLE's attribute tables directly, so this is a re-implementation
// against the same public API rather than a copy of its internals. The parts that
// have to agree byte for byte — the Report Descriptors and the composition rules —
// live in internal/EspBleBluedroidHidReportMaps.h and are pinned by
// tests/unit/hid_report_maps.
//
// Notably, the manager is written against this library's *public* GATT Server API,
// the same one a sketch has: the duplicate-characteristic-UUID support it needs is
// what `peer/duplicate_uuid_server` verifies, and the events it observes are taken
// with add*Listener() so an application can still install its own callbacks
// (`peer/multi_listener`). A profile helper that needed private access would be a
// sign the public API was missing something.
//
// The attribute layout is HOGP's:
//
//   HID Service 0x1812
//     Report Map        0x2A4B  read
//     HID Information   0x2A4A  read
//     HID Control Point 0x2A4C  write without response
//     Protocol Mode     0x2A4E  read + write without response
//     Report            0x2A4D  notify + read, Report Reference {id, Input}
//     Report            0x2A4D  read + write + write without response,
//                               Report Reference {id, Output}
//     Boot Keyboard Input  0x2A22  notify + read      (bootProtocol only)
//     Boot Keyboard Output 0x2A32  read + write       (bootProtocol only)
//   Battery Service 0x180F
//     Battery Level     0x2A19  read + notify
//   Device Information 0x180A
//     Manufacturer Name 0x2A29  read
//     PnP ID            0x2A50  read
//
// The two Report characteristics share UUID 0x2A4D and are told apart by their
// Report Reference descriptor, which is why this profile had to wait for duplicate
// characteristic UUIDs.

#include "EspBleBluedroid.h"

#include "internal/EspBleBluedroidHidReportMaps.h"

#include <cstring>

namespace hid = espblebluedroid::internal::hid;

namespace
{
constexpr const char *HidServiceUuid = "1812";
constexpr const char *ReportMapUuid = "2a4b";
constexpr const char *HidInformationUuid = "2a4a";
constexpr const char *HidControlPointUuid = "2a4c";
constexpr const char *ProtocolModeUuid = "2a4e";
constexpr const char *ReportUuid = "2a4d";
constexpr const char *ReportReferenceUuid = "2908";
constexpr const char *BootKeyboardInputUuid = "2a22";
constexpr const char *BootKeyboardOutputUuid = "2a32";
constexpr const char *BatteryServiceUuid = "180f";
constexpr const char *BatteryLevelUuid = "2a19";
constexpr const char *DeviceInformationServiceUuid = "180a";
constexpr const char *ManufacturerNameUuid = "2a29";
constexpr const char *PnpIdUuid = "2a50";

// The Boot Keyboard Input Report is always this fixed layout, whatever the
// Report-protocol descriptor says: [modifiers, reserved, keycode1..6].
constexpr size_t BootReportLength = 8;

// GAP appearance for a HID keyboard, so a host lists the device with the right
// icon before it has read anything from the HID service.
constexpr uint16_t HidKeyboardAppearance = 0x03c1;
}  // namespace

// ---------------------------------------------------------------------------
// The state shared by every HID device profile of one device. Only the keyboard
// profile is implemented so far; the structure is the one the others will join,
// because HOGP puts every profile of a device in a single HID service and tells
// the reports apart by Report ID.
// ---------------------------------------------------------------------------
struct EspBleHidDeviceManagerImpl
{
  static constexpr size_t MaxSubscribers = 4;
  // The vendor profile's report is sized by the caller, and it is the longest any
  // profile sends; the longest fixed one is the 29-byte NKRO keyboard report.
  static constexpr size_t MaxVendorReportSize = 64;
  static constexpr size_t MaxInputReportLength = MaxVendorReportSize;
  static constexpr size_t MaxCustomReports = EspBleHidCustom::MaxReports;
  static constexpr size_t CustomReportMapCapacity = hid::CustomReportMapCapacity;

  struct SubscriptionSlot
  {
    bool used = false;
    EspBleConnectionId connectionId = 0;
    // One bit per profile, by report ID: a host subscribes to each Input Report
    // separately, so "is anyone listening" is a per-profile question.
    uint8_t inputNotifications = 0;
    // The same, one bit per declared hidCustom() report slot.
    uint8_t customNotifications = 0;
    bool bootKeyboardNotifications = false;
    bool batteryNotifications = false;
  };

  // One caller-declared report of hidCustom(): its own 0x2A4D characteristic with
  // a Report Reference naming the ID and the type the caller gave it.
  struct CustomReport
  {
    uint8_t reportId = 0;
    uint8_t reportType = 0;
    uint16_t size = 0;
    EspBleGattCharacteristic characteristic;
    EspBleGattDescriptor reference;
  };

  explicit EspBleHidDeviceManagerImpl(EspBleBluedroid *owner) : owner(owner) {}

  EspBleBluedroid *owner;

  EspBleHidKeyboardConfig config;
  bool configured = false;
  bool realized = false;
  bool securityEnabled = false;
  uint8_t profileMask = 0;
  bool keyboardNkro = false;
  bool bootProtocol = false;

  // Registered attributes.
  EspBleGattService hidService;
  EspBleGattCharacteristic reportMap;
  EspBleGattCharacteristic hidInformation;
  EspBleGattCharacteristic hidControlPoint;
  EspBleGattCharacteristic protocolModeCharacteristic;
  // One Input Report characteristic per profile, indexed by report ID - 1. They
  // all share UUID 0x2A4D and are told apart by their Report Reference descriptor.
  EspBleGattCharacteristic inputReports[hid::ProfileCount];
  EspBleGattDescriptor inputReferences[hid::ProfileCount];
  EspBleGattCharacteristic keyboardOutput;
  EspBleGattDescriptor keyboardOutputReference;
  // The vendor profile's own Output and Feature reports, the only fixed profile
  // that has any: everything else is notify-only.
  EspBleGattCharacteristic vendorOutput;
  EspBleGattDescriptor vendorOutputReference;
  EspBleGattCharacteristic vendorFeature;
  EspBleGattDescriptor vendorFeatureReference;
  EspBleGattCharacteristic bootKeyboardInput;
  EspBleGattCharacteristic bootKeyboardOutput;
  EspBleGattCharacteristic batteryLevelCharacteristic;
  EspBleGattCharacteristic manufacturerName;
  EspBleGattCharacteristic pnpId;

  uint8_t protocolMode = EspBleHidKeyboard::ReportProtocolMode;
  uint8_t batteryLevel = 100;
  uint8_t mouseButtonCount = 5;
  uint8_t vendorReportSize = 63;
  EspBleHidKeyboardOutputReport ledState;

  bool customConfigured = false;
  CustomReport customReports[MaxCustomReports];
  size_t customReportCount = 0;
  // The caller's Report Descriptor, appended to the composed one so a custom
  // report can share the HID service with the fixed profiles.
  uint8_t customReportMap[CustomReportMapCapacity] = {};
  size_t customReportMapLength = 0;

  SubscriptionSlot subscribers[MaxSubscribers];

  SubscriptionSlot *slotFor(EspBleConnectionId connectionId, bool create)
  {
    for (SubscriptionSlot &slot : subscribers)
    {
      if (slot.used && slot.connectionId == connectionId) return &slot;
    }
    if (!create) return nullptr;
    for (SubscriptionSlot &slot : subscribers)
    {
      if (!slot.used)
      {
        slot = SubscriptionSlot();
        slot.used = true;
        slot.connectionId = connectionId;
        return &slot;
      }
    }
    return nullptr;
  }

  // The link this device is the peripheral on, if there is one. `connection()`
  // resolves a connection ID rather than enumerating, and HID only ever talks to
  // the peripheral link, so the ID comes from the owner.
  bool peripheralConnection(EspBleConnection &connection) const
  {
    const EspBleConnectionId id = owner->peripheralConnectionId();
    if (id == 0) return false;
    return owner->connection(id, connection);
  }

  static uint8_t inputBit(uint8_t reportId)
  {
    return static_cast<uint8_t>(1u << (reportId - 1));
  }

  EspBleGattCharacteristic inputReport(uint8_t reportId) const
  {
    if (reportId < 1 || reportId > hid::ProfileCount)
      return EspBleGattCharacteristic();
    return inputReports[reportId - 1];
  }

  void forgetConnection(EspBleConnectionId connectionId)
  {
    SubscriptionSlot *slot = slotFor(connectionId, false);
    if (slot != nullptr) *slot = SubscriptionSlot();
  }

  // Rebuild the Report Map from the profiles configured so far. Called from each
  // profile's configure(), so the map holds exactly what is registered — one HID
  // service carries every profile of the device.
  bool recomposeReportMap()
  {
    uint8_t buffer[hid::ReportMapCapacity + CustomReportMapCapacity];
    size_t length = 0;
    if (profileMask != 0)
    {
      length = hid::compose(buffer, hid::ReportMapCapacity, profileMask,
        keyboardNkro, mouseButtonCount, vendorReportSize);
      if (length == 0) return false;
    }
    // The caller's descriptor follows the composed one, so both sets of reports
    // are declared in the single Report Map HOGP allows.
    if (customReportMapLength > 0)
    {
      memcpy(buffer + length, customReportMap, customReportMapLength);
      length += customReportMapLength;
    }
    if (length == 0)
    {
      // hidCustom() before setReportMap(): nothing to publish yet, and the value
      // is set again from setReportMap().
      return true;
    }
    return owner->gattServer().setValue(reportMap, buffer, length);
  }
};

// ---------------------------------------------------------------------------
// EspBleHidKeyboard
// ---------------------------------------------------------------------------

EspBleHidKeyboard::EspBleHidKeyboard(EspBleBluedroid *owner) : owner_(owner) {}

EspBleHidKeyboard::~EspBleHidKeyboard()
{
  delete impl_;
  impl_ = nullptr;
}

void EspBleHidKeyboard::enableNkro(bool enable)
{
  // Before configure(): the descriptor decides the report layout, and it is built
  // when the attributes are registered.
  if (impl_ != nullptr && impl_->configured) return;
  nkroEnabled_ = enable;
}

bool EspBleHidKeyboard::nkroEnabled() const { return nkroEnabled_; }

bool EspBleHidKeyboard::configured() const
{
  return impl_ != nullptr && impl_->configured;
}

bool EspBleHidKeyboard::configureCommon(const EspBleHidDeviceConfig &config)
{
  if (owner_->initialized())
  {
    owner_->setError(EspBleError::InvalidState,
      "configure HID profiles before begin()");
    return false;
  }
  if (config.initialBatteryLevel > 100)
  {
    owner_->setError(EspBleError::InvalidArgument,
      "HID battery level must be at most 100");
    return false;
  }
  if (impl_ == nullptr)
  {
    impl_ = new EspBleHidDeviceManagerImpl(owner_);
    if (impl_ == nullptr)
    {
      owner_->setError(EspBleError::ResourceExhausted,
        "failed to allocate HID Device state");
      return false;
    }
  }
  if (impl_->configured)
  {
    // Re-configuring is accepted and re-applies the values, but the attributes are
    // registered once: a second registration would publish a second HID service.
    impl_->batteryLevel = config.initialBatteryLevel;
    owner_->clearError();
    return true;
  }

  auto &server = owner_->gattServer();

  EspBleGattCharacteristicConfig readOnly;
  readOnly.readable = true;
  EspBleGattCharacteristicConfig writeOnly;
  writeOnly.writable = true;
  writeOnly.writableWithoutResponse = true;
  EspBleGattCharacteristicConfig readWriteNoResponse;
  readWriteNoResponse.readable = true;
  readWriteNoResponse.writable = true;
  readWriteNoResponse.writableWithoutResponse = true;
  EspBleGattCharacteristicConfig inputReport;
  inputReport.readable = true;
  inputReport.notifiable = true;
  EspBleGattCharacteristicConfig outputReport;
  outputReport.readable = true;
  outputReport.writable = true;
  outputReport.writableWithoutResponse = true;
  EspBleGattCharacteristicConfig batteryConfig;
  batteryConfig.readable = true;
  batteryConfig.notifiable = true;

  impl_->hidService = server.addService(HidServiceUuid);
  impl_->reportMap = server.addCharacteristic(
    impl_->hidService, ReportMapUuid, readOnly);
  impl_->hidInformation = server.addCharacteristic(
    impl_->hidService, HidInformationUuid, readOnly);
  impl_->hidControlPoint = server.addCharacteristic(
    impl_->hidService, HidControlPointUuid, writeOnly);
  impl_->protocolModeCharacteristic = server.addCharacteristic(
    impl_->hidService, ProtocolModeUuid, readWriteNoResponse);

  const EspBleGattService battery = server.addService(BatteryServiceUuid);
  impl_->batteryLevelCharacteristic =
    server.addCharacteristic(battery, BatteryLevelUuid, batteryConfig);

  const EspBleGattService information =
    server.addService(DeviceInformationServiceUuid);
  impl_->manufacturerName =
    server.addCharacteristic(information, ManufacturerNameUuid, readOnly);
  impl_->pnpId = server.addCharacteristic(information, PnpIdUuid, readOnly);

  const bool registered = impl_->hidService && impl_->reportMap &&
    impl_->hidInformation && impl_->hidControlPoint &&
    impl_->protocolModeCharacteristic && battery &&
    impl_->batteryLevelCharacteristic && information && impl_->manufacturerName &&
    impl_->pnpId;
  if (!registered)
  {
    // lastError() already names what failed (usually too many attributes).
    return false;
  }

  uint8_t hidInformationValue[sizeof(hid::HidInformation)];
  memcpy(hidInformationValue, hid::HidInformation, sizeof(hidInformationValue));
  hidInformationValue[hid::HidInformationCountryOffset] = config.countryCode;
  uint8_t pnpIdValue[hid::PnpIdLength];
  hid::composePnpId(
    pnpIdValue, config.vendorId, config.productId, config.productVersion);
  const uint8_t protocolModeValue = ReportProtocolMode;

  impl_->protocolMode = ReportProtocolMode;
  impl_->batteryLevel = config.initialBatteryLevel;

  const bool values =
    server.setValue(
      impl_->hidInformation, hidInformationValue, sizeof(hidInformationValue)) &&
    server.setValue(impl_->protocolModeCharacteristic, &protocolModeValue, 1) &&
    server.setValue(impl_->batteryLevelCharacteristic, &impl_->batteryLevel, 1) &&
    server.setValue(impl_->manufacturerName,
      String(config.manufacturer == nullptr ? "" : config.manufacturer)) &&
    server.setValue(impl_->pnpId, pnpIdValue, sizeof(pnpIdValue));
  if (!values)
  {
    return false;
  }
  // A host finds a HID device by the service UUID in the advertisement.
  if (!owner_->advertising().addServiceUuid(HidServiceUuid))
  {
    return false;
  }

  // The events this profile needs, taken as additional observers so a sketch can
  // still install its own on*() callbacks for the same events.
  EspBleHidDeviceManagerImpl *impl = impl_;
  EspBleHidKeyboard *keyboard = this;
  server.addSubscriptionChangedListener(
    [impl](const EspBleGattSubscription &subscription) {
      EspBleHidDeviceManagerImpl::SubscriptionSlot *slot =
        impl->slotFor(subscription.connectionId, subscription.notifications);
      if (slot == nullptr) return;
      for (uint8_t reportId = 1; reportId <= hid::ProfileCount; ++reportId)
      {
        if (!(subscription.characteristic == impl->inputReports[reportId - 1]))
          continue;
        const uint8_t bit = EspBleHidDeviceManagerImpl::inputBit(reportId);
        if (subscription.notifications) slot->inputNotifications |= bit;
        else slot->inputNotifications &= static_cast<uint8_t>(~bit);
        return;
      }
      for (size_t slotIndex = 0; slotIndex < impl->customReportCount; ++slotIndex)
      {
        if (!(subscription.characteristic ==
              impl->customReports[slotIndex].characteristic))
        {
          continue;
        }
        const uint8_t bit = static_cast<uint8_t>(1u << slotIndex);
        if (subscription.notifications) slot->customNotifications |= bit;
        else slot->customNotifications &= static_cast<uint8_t>(~bit);
        return;
      }
      if (subscription.characteristic == impl->bootKeyboardInput)
        slot->bootKeyboardNotifications = subscription.notifications;
      else if (subscription.characteristic == impl->batteryLevelCharacteristic)
        slot->batteryNotifications = subscription.notifications;
    });
  server.addWrittenListener([impl, keyboard](const EspBleGattWrite &write) {
    if (write.characteristic == impl->protocolModeCharacteristic)
    {
      if (write.value.length() == 0) return;
      const uint8_t mode = static_cast<uint8_t>(write.value[0]);
      if (mode != BootProtocolMode && mode != ReportProtocolMode) return;
      impl->protocolMode = mode;
      impl->owner->gattServer().setValue(
        impl->protocolModeCharacteristic, &mode, 1);
      if (keyboard->protocolModeCallback_)
        keyboard->protocolModeCallback_(mode, write.connectionId);
      return;
    }
    // The profiles whose payload the library does not interpret hand the bytes
    // straight to the sketch. No queue is needed for any of this: the listener
    // already runs in the caller's update() context, which is where EspBle's
    // dispatchPendingReports() delivers them from.
    if (impl->vendorOutput.valid() &&
        (write.characteristic == impl->vendorOutput ||
          write.characteristic == impl->vendorFeature))
    {
      const bool feature = write.characteristic == impl->vendorFeature;
      EspBleHidVendor &vendor = impl->owner->hidVendor();
      EspBleHidVendor::ReportCallback &callback =
        feature ? vendor.featureCallback_ : vendor.outputCallback_;
      if (!callback) return;
      EspBleHidVendorReport report;
      report.connectionId = write.connectionId;
      report.reportId = ESP_BLE_HID_REPORT_ID_VENDOR;
      report.reportType = feature ? ESP_BLE_HID_REPORT_TYPE_FEATURE
                                  : ESP_BLE_HID_REPORT_TYPE_OUTPUT;
      report.rawData = reinterpret_cast<const uint8_t *>(write.value.c_str());
      report.rawLength = write.value.length();
      report.data = report.rawData;
      report.length = report.rawLength;
      callback(report);
      return;
    }
    for (size_t slotIndex = 0; slotIndex < impl->customReportCount; ++slotIndex)
    {
      const EspBleHidDeviceManagerImpl::CustomReport &custom =
        impl->customReports[slotIndex];
      if (!(write.characteristic == custom.characteristic)) continue;
      EspBleHidCustom &customProfile = impl->owner->hidCustom();
      EspBleHidCustom::ReportCallback &callback =
        custom.reportType == ESP_BLE_HID_REPORT_TYPE_FEATURE
          ? customProfile.featureCallback_
          : customProfile.outputCallback_;
      if (!callback) return;
      EspBleHidVendorReport report;
      report.connectionId = write.connectionId;
      report.reportId = custom.reportId;
      report.reportType = custom.reportType;
      report.rawData = reinterpret_cast<const uint8_t *>(write.value.c_str());
      report.rawLength = write.value.length();
      report.data = report.rawData;
      report.length = report.rawLength;
      callback(report);
      return;
    }
    const bool isOutput = write.characteristic == impl->keyboardOutput ||
      (impl->bootProtocol && write.characteristic == impl->bootKeyboardOutput);
    if (!isOutput || write.value.length() == 0) return;
    // The LED state and the callback are updated together here, because this
    // listener already runs in the caller's update() context.
    EspBleHidKeyboardOutputReport report;
    report.connectionId = write.connectionId;
    report.setLeds(static_cast<uint8_t>(write.value[0]));
    impl->ledState = report;
    if (keyboard->outputReportCallback_) keyboard->outputReportCallback_(report);
  });
  // A disconnected host's subscription and LED state must not be reported as the
  // current one's.
  owner_->addDisconnectedListener([impl](const EspBleConnection &connection) {
    impl->forgetConnection(connection.id);
    bool anySubscriber = false;
    for (const auto &slot : impl->subscribers) anySubscriber |= slot.used;
    if (!anySubscriber)
    {
      impl->ledState = EspBleHidKeyboardOutputReport();
      impl->protocolMode = ReportProtocolMode;
    }
  });

  impl_->configured = true;
  owner_->clearError();
  return true;
}

bool EspBleHidKeyboard::configure(const EspBleHidKeyboardConfig &config)
{
  const bool alreadyConfigured = impl_ != nullptr && impl_->configured;
  const bool alreadyKeyboard = alreadyConfigured &&
    impl_->inputReports[ESP_BLE_HID_REPORT_ID_KEYBOARD - 1].valid();
  if (!alreadyConfigured)
  {
    // The keyboard's own settings have to be known before the shared part is
    // built: Boot Protocol adds two characteristics to the HID service, and NKRO
    // changes the descriptor the Report Map is composed from.
    if (impl_ == nullptr)
    {
      // configureCommon() allocates it, but bootProtocol/NKRO must be in place
      // first, so allocate here when this is the first call of any kind.
      impl_ = new EspBleHidDeviceManagerImpl(owner_);
      if (impl_ == nullptr)
      {
        owner_->setError(EspBleError::ResourceExhausted,
          "failed to allocate HID Device state");
        return false;
      }
    }
    impl_->keyboardNkro = nkroEnabled_;
    impl_->bootProtocol = config.bootProtocol;
  }
  if (!configureCommon(config)) return false;
  impl_->config = config;
  layout_ = config.layout;
  if (alreadyKeyboard)
  {
    owner_->clearError();
    return true;
  }

  auto &server = owner_->gattServer();
  EspBleGattCharacteristicConfig inputReport;
  inputReport.readable = true;
  inputReport.notifiable = true;
  EspBleGattCharacteristicConfig outputReport;
  outputReport.readable = true;
  outputReport.writable = true;
  outputReport.writableWithoutResponse = true;

  // Two characteristics with one UUID, told apart by their Report Reference
  // descriptor. This is the shape HOGP defines and the reason the duplicate-UUID
  // restriction had to go (peer/duplicate_uuid_server).
  if (!configureProfile(ESP_BLE_HID_REPORT_ID_KEYBOARD, config)) return false;
  impl_->keyboardOutput = server.addCharacteristic(
    impl_->hidService, ReportUuid, outputReport);
  impl_->keyboardOutputReference = server.addDescriptor(
    impl_->keyboardOutput, ReportReferenceUuid);
  if (impl_->bootProtocol)
  {
    impl_->bootKeyboardInput = server.addCharacteristic(
      impl_->hidService, BootKeyboardInputUuid, inputReport);
    impl_->bootKeyboardOutput = server.addCharacteristic(
      impl_->hidService, BootKeyboardOutputUuid, outputReport);
  }
  if (!impl_->keyboardOutput || !impl_->keyboardOutputReference ||
      (impl_->bootProtocol &&
        (!impl_->bootKeyboardInput || !impl_->bootKeyboardOutput)))
  {
    return false;
  }

  const uint8_t outputReference[2] =
    {ESP_BLE_HID_REPORT_ID_KEYBOARD, ESP_BLE_HID_REPORT_TYPE_OUTPUT};
  const uint8_t emptyKeyboardReport[BootReportLength] = {};
  const bool values =
    server.setDescriptorValue(
      impl_->keyboardOutputReference, outputReference, sizeof(outputReference)) &&
    server.setValue(impl_->inputReports[ESP_BLE_HID_REPORT_ID_KEYBOARD - 1],
      emptyKeyboardReport,
      impl_->keyboardNkro ? 1 + EspBleHidKeyboardNkroReport::BitmapSize
                          : BootReportLength) &&
    server.setValue(impl_->keyboardOutput, emptyKeyboardReport, 1) &&
    (!impl_->bootProtocol ||
      (server.setValue(impl_->bootKeyboardInput, emptyKeyboardReport,
         BootReportLength) &&
        server.setValue(impl_->bootKeyboardOutput, emptyKeyboardReport, 1)));
  if (!values) return false;
  // A host shows the device with the keyboard icon because of the appearance.
  owner_->advertising().setAppearance(HidKeyboardAppearance);
  owner_->clearError();
  return true;
}

bool EspBleHidKeyboard::applySecurity(bool securityEnabled)
{
  if (impl_ == nullptr || !impl_->configured) return true;
  impl_->securityEnabled = securityEnabled;
  impl_->realized = true;
  if (!securityEnabled) return true;

  // HOGP requires Security Mode 1 Level 2 on the HID attributes; the
  // insufficient-encryption error a host gets on an unencrypted link is what
  // makes its OS start pairing.
  auto &server = owner_->gattServer();
  const EspBleGattCharacteristic readable[] = {
    impl_->reportMap, impl_->hidInformation, impl_->pnpId,
  };
  for (const EspBleGattCharacteristic &characteristic : readable)
  {
    if (!server.setEncryptionRequirement(characteristic, true, false)) return false;
  }
  const EspBleGattCharacteristic readWrite[] = {
    impl_->hidControlPoint, impl_->protocolModeCharacteristic,
    impl_->keyboardOutput, impl_->bootKeyboardInput, impl_->bootKeyboardOutput,
    impl_->vendorOutput, impl_->vendorFeature,
  };
  for (const EspBleGattCharacteristic &characteristic : readWrite)
  {
    if (!characteristic.valid()) continue;
    if (!server.setEncryptionRequirement(characteristic, true, true)) return false;
  }
  // Every profile's Input Report and its Report Reference, not just the
  // keyboard's: a profile configured later must not be the unprotected one.
  for (size_t index = 0; index < hid::ProfileCount; ++index)
  {
    if (impl_->inputReports[index].valid() &&
        !server.setEncryptionRequirement(impl_->inputReports[index], true, true))
    {
      return false;
    }
    if (impl_->inputReferences[index].valid() &&
        !server.setDescriptorEncryptionRequirement(
          impl_->inputReferences[index], true))
    {
      return false;
    }
  }
  // The caller-declared reports of hidCustom() are HID attributes too, whatever
  // their payload means.
  for (size_t index = 0; index < impl_->customReportCount; ++index)
  {
    const EspBleHidDeviceManagerImpl::CustomReport &report =
      impl_->customReports[index];
    if (!server.setEncryptionRequirement(report.characteristic, true, true) ||
        !server.setDescriptorEncryptionRequirement(report.reference, true))
    {
      return false;
    }
  }
  const EspBleGattDescriptor references[] = {
    impl_->keyboardOutputReference, impl_->vendorOutputReference,
    impl_->vendorFeatureReference,
  };
  for (const EspBleGattDescriptor &reference : references)
  {
    if (!reference.valid()) continue;
    if (!server.setDescriptorEncryptionRequirement(reference, true)) return false;
  }
  return true;
}

void EspBleHidKeyboard::resetBackend()
{
  if (impl_ == nullptr) return;
  impl_->realized = false;
  for (auto &slot : impl_->subscribers) slot = {};
  impl_->ledState = EspBleHidKeyboardOutputReport();
  impl_->protocolMode = ReportProtocolMode;
  nkroState_.clear();
}

bool EspBleHidKeyboard::useBootKeyboard(uint8_t reportId) const
{
  // In Boot Protocol Mode the keyboard report travels over the dedicated 8-byte
  // Boot Keyboard Input Report instead of the Report-protocol characteristic.
  if (reportId != ESP_BLE_HID_REPORT_ID_KEYBOARD || impl_ == nullptr ||
      !impl_->bootProtocol || !impl_->bootKeyboardInput.valid())
  {
    return false;
  }
  return impl_->protocolMode == BootProtocolMode;
}

bool EspBleHidKeyboard::readyFor(uint8_t reportId) const
{
  if (impl_ == nullptr || !impl_->configured || !impl_->realized ||
      !owner_->initialized() || !impl_->inputReport(reportId).valid())
  {
    return false;
  }
  const bool useBoot = useBootKeyboard(reportId);
  EspBleConnection connection;
  // HID input only ever goes to the link this device is the peripheral on, and
  // that is the one link the GATT Server exposes.
  if (!impl_->peripheralConnection(connection)) return false;
  // HOGP: never push HID data over an unencrypted link when security is on.
  if (impl_->securityEnabled && !connection.encrypted) return false;
  const EspBleHidDeviceManagerImpl::SubscriptionSlot *slot =
    impl_->slotFor(connection.id, false);
  if (slot == nullptr) return false;
  if (useBoot) return slot->bootKeyboardNotifications;
  return (slot->inputNotifications &
    EspBleHidDeviceManagerImpl::inputBit(reportId)) != 0;
}

bool EspBleHidKeyboard::ready() const
{
  return readyFor(ESP_BLE_HID_REPORT_ID_KEYBOARD);
}

bool EspBleHidKeyboard::sendRawReport(
  uint8_t reportId, const uint8_t *data, size_t length)
{
  if (impl_ == nullptr || !impl_->configured || !impl_->realized ||
      !owner_->initialized() || !impl_->inputReport(reportId).valid())
  {
    owner_->setError(EspBleError::InvalidState,
      "HID Device profile is not initialized");
    return false;
  }
  if (data == nullptr || length == 0 ||
      length > EspBleHidDeviceManagerImpl::MaxInputReportLength)
  {
    owner_->setError(EspBleError::InvalidArgument, "invalid HID input report");
    return false;
  }

  const bool useBoot = useBootKeyboard(reportId);
  uint8_t boot[BootReportLength] = {};
  const uint8_t *value = data;
  size_t valueLength = length;
  if (useBoot)
  {
    // The Boot Keyboard Input Report is always [modifiers, reserved, key1..6]. A
    // 6KRO report already matches; an NKRO bitmap is down-converted to keycodes,
    // and too many held keys become the HID rollover code 0x01.
    if (length == BootReportLength)
    {
      memcpy(boot, data, BootReportLength);
    }
    else
    {
      boot[0] = data[0];
      size_t keyIndex = 2;
      for (size_t byte = 1; byte < length; ++byte)
      {
        for (uint8_t bit = 0; bit < 8; ++bit)
        {
          if ((data[byte] & static_cast<uint8_t>(1u << bit)) == 0) continue;
          if (keyIndex >= BootReportLength)
          {
            memset(boot + 2, 0x01, 6);
            byte = length;
            break;
          }
          boot[keyIndex++] = static_cast<uint8_t>(((byte - 1) << 3) + bit);
        }
      }
    }
    value = boot;
    valueLength = BootReportLength;
  }

  auto &server = owner_->gattServer();
  const EspBleGattCharacteristic characteristic =
    useBoot ? impl_->bootKeyboardInput : impl_->inputReport(reportId);

  bool sent = false;
  EspBleConnection connection;
  const bool anyPeripheral = impl_->peripheralConnection(connection);
  if (anyPeripheral && (!impl_->securityEnabled || connection.encrypted))
  {
    const EspBleHidDeviceManagerImpl::SubscriptionSlot *slot =
      impl_->slotFor(connection.id, false);
    const bool subscribed = slot != nullptr &&
      (useBoot ? slot->bootKeyboardNotifications
               : (slot->inputNotifications &
                   EspBleHidDeviceManagerImpl::inputBit(reportId)) != 0);
    if (subscribed)
    {
      sent = server.notify(connection.id, characteristic, value, valueLength);
    }
  }
  if (!sent)
  {
    // The two states a caller has to tell apart: nobody is there, or a host is
    // there but has not subscribed (or paired) yet.
    owner_->setError(EspBleError::InvalidState,
      anyPeripheral ? "no subscribed HID Host" : "no connected HID Host");
    return false;
  }
  owner_->clearError();
  return true;
}

bool EspBleHidKeyboard::sendReport(const EspBleHidKeyboardReport &report)
{
  if (nkroEnabled_)
  {
    // A 6-key report in NKRO mode is expanded into the bitmap, so the wire format
    // stays the one the descriptor declares.
    nkroState_.clear();
    nkroState_.modifiers = report.modifiers;
    for (uint8_t key : report.keys)
    {
      if (key != 0) nkroState_.press(key);
    }
    return sendHeldNkroState();
  }
  uint8_t value[BootReportLength] = {};
  value[0] = report.modifiers;
  memcpy(value + 2, report.keys, sizeof(report.keys));
  return sendRawReport(ESP_BLE_HID_REPORT_ID_KEYBOARD, value, sizeof(value));
}

bool EspBleHidKeyboard::sendReport(const EspBleHidKeyboardNkroReport &report)
{
  if (!nkroEnabled_)
  {
    owner_->setError(EspBleError::InvalidState,
      "enableNkro() must be called before configure() to send an NKRO report");
    return false;
  }
  // Replace the incremental state, so a later pressUsage() / releaseUsage() sees
  // what the host was actually told.
  nkroState_ = report;
  return sendHeldNkroState();
}

bool EspBleHidKeyboard::sendHeldNkroState()
{
  uint8_t value[1 + EspBleHidKeyboardNkroReport::BitmapSize] = {};
  value[0] = nkroState_.modifiers;
  memcpy(value + 1, nkroState_.bitmap, sizeof(nkroState_.bitmap));
  return sendRawReport(ESP_BLE_HID_REPORT_ID_KEYBOARD, value, sizeof(value));
}

const EspBleHidKeyboardNkroReport &EspBleHidKeyboard::heldState() const
{
  return nkroState_;
}

bool EspBleHidKeyboard::pressUsage(uint8_t usage, uint8_t modifiers, uint32_t)
{
  if (nkroEnabled_)
  {
    if (!nkroState_.press(usage))
    {
      owner_->setError(EspBleError::InvalidArgument,
        "NKRO keyboard usage must be at most 0xe7");
      return false;
    }
    nkroState_.modifiers |= modifiers;
    return sendHeldNkroState();
  }
  EspBleHidKeyboardReport report;
  report.modifiers = modifiers;
  report.keys[0] = usage;
  return sendReport(report);
}

bool EspBleHidKeyboard::releaseUsage(uint8_t usage)
{
  // Without NKRO there is no per-key state to subtract from: a 6KRO report holds
  // whatever was sent last, so releasing one key means releasing everything.
  if (!nkroEnabled_) return releaseAll();
  if (!nkroState_.release(usage))
  {
    owner_->setError(EspBleError::InvalidArgument,
      "NKRO keyboard usage must be at most 0xe7");
    return false;
  }
  return sendHeldNkroState();
}

bool EspBleHidKeyboard::tapUsage(uint8_t usage, uint8_t modifiers, uint32_t holdMs)
{
  if (!pressUsage(usage, modifiers)) return false;
  delay(holdMs);
  return releaseAll();
}

bool EspBleHidKeyboard::pressKey(char key, uint32_t)
{
  // The layout tables map usage + modifier to a character, so the reverse lookup
  // is a search: one table, one direction, no second copy to keep in step.
  const uint8_t modifiers[] = {0, EspBleHidKeyboardReport::LeftShift,
    EspBleHidKeyboardReport::RightAlt,
    static_cast<uint8_t>(EspBleHidKeyboardReport::LeftShift |
                         EspBleHidKeyboardReport::RightAlt)};
  for (uint8_t modifier : modifiers)
  {
    for (uint16_t usage = 1; usage < 256; ++usage)
    {
      if (espBleUsageToUnicode(
            static_cast<uint8_t>(usage), modifier, layout_, false, false) ==
          static_cast<uint8_t>(key))
      {
        return pressUsage(static_cast<uint8_t>(usage), modifier);
      }
    }
  }
  owner_->setError(EspBleError::InvalidArgument,
    "character is not available in keyboard layout");
  return false;
}

bool EspBleHidKeyboard::tapKey(char key, uint32_t holdMs)
{
  if (!pressKey(key)) return false;
  delay(holdMs);
  return releaseAll();
}

bool EspBleHidKeyboard::write(const char *text, uint32_t interKeyDelayMs)
{
  if (text == nullptr)
  {
    owner_->setError(EspBleError::InvalidArgument, "text must not be null");
    return false;
  }
  for (const char *cursor = text; *cursor != '\0'; ++cursor)
  {
    if (!tapKey(*cursor)) return false;
    if (interKeyDelayMs != 0) delay(interKeyDelayMs);
  }
  return true;
}

bool EspBleHidKeyboard::releaseAll()
{
  nkroState_.clear();
  return sendReport(EspBleHidKeyboardReport());
}

void EspBleHidKeyboard::setLayout(EspBleKeyboardLayout layout)
{
  layout_ = layout;
}

EspBleKeyboardLayout EspBleHidKeyboard::layout() const { return layout_; }

bool EspBleHidKeyboard::setBatteryLevel(uint8_t level)
{
  if (level > 100)
  {
    owner_->setError(EspBleError::InvalidArgument,
      "battery level must be between 0 and 100");
    return false;
  }
  if (impl_ == nullptr || !impl_->configured)
  {
    owner_->setError(EspBleError::InvalidState,
      "configure the HID Device before setting the battery level");
    return false;
  }
  impl_->batteryLevel = level;
  auto &server = owner_->gattServer();
  if (!server.setValue(impl_->batteryLevelCharacteristic, &level, 1))
  {
    return false;
  }
  if (!impl_->realized || !owner_->initialized())
  {
    // Before begin() the value is simply the one a host will read first.
    owner_->clearError();
    return true;
  }
  EspBleConnection connection;
  if (impl_->peripheralConnection(connection))
  {
    const EspBleHidDeviceManagerImpl::SubscriptionSlot *slot =
      impl_->slotFor(connection.id, false);
    if (slot != nullptr && slot->batteryNotifications)
    {
      server.notify(connection.id, impl_->batteryLevelCharacteristic, &level, 1);
    }
  }
  owner_->clearError();
  return true;
}

void EspBleHidKeyboard::onOutputReport(OutputReportCallback callback)
{
  outputReportCallback_ = callback;
}

EspBleHidKeyboardOutputReport EspBleHidKeyboard::ledState() const
{
  if (impl_ == nullptr) return EspBleHidKeyboardOutputReport();
  return impl_->ledState;
}

uint8_t EspBleHidKeyboard::protocolMode() const
{
  if (impl_ == nullptr) return ReportProtocolMode;
  return impl_->protocolMode;
}

void EspBleHidKeyboard::onProtocolMode(ProtocolModeCallback callback)
{
  protocolModeCallback_ = callback;
}

// Register one more profile in the HID service the keyboard set up. Called from
// each other profile's configure(), which is why they all have to run before
// begin(): the Report Map and the attribute table are built once.
bool EspBleHidKeyboard::configureProfile(
  uint8_t reportId, const EspBleHidDeviceConfig &config)
{
  if (owner_->initialized())
  {
    owner_->setError(EspBleError::InvalidState,
      "configure HID profiles before begin()");
    return false;
  }
  if (reportId < 1 || reportId > hid::ProfileCount)
  {
    owner_->setError(EspBleError::InvalidArgument, "unknown HID profile");
    return false;
  }
  // A profile can be configured without the keyboard: whoever comes first brings
  // up the HID service. The keyboard's own reports are only added by configure().
  if (!configureCommon(config)) return false;
  if (impl_->inputReports[reportId - 1].valid())
  {
    owner_->clearError();
    return true;  // already registered; re-configuring only re-applies values
  }

  auto &server = owner_->gattServer();
  EspBleGattCharacteristicConfig inputConfig;
  inputConfig.readable = true;
  inputConfig.notifiable = true;
  impl_->inputReports[reportId - 1] =
    server.addCharacteristic(impl_->hidService, ReportUuid, inputConfig);
  impl_->inputReferences[reportId - 1] = server.addDescriptor(
    impl_->inputReports[reportId - 1], ReportReferenceUuid);
  if (!impl_->inputReports[reportId - 1] || !impl_->inputReferences[reportId - 1])
  {
    return false;
  }
  const uint8_t reference[2] = {reportId, ESP_BLE_HID_REPORT_TYPE_INPUT};
  if (!server.setDescriptorValue(
        impl_->inputReferences[reportId - 1], reference, sizeof(reference)))
  {
    return false;
  }
  impl_->profileMask |= static_cast<uint8_t>(1u << (reportId - 1));
  if (!impl_->recomposeReportMap()) return false;
  owner_->clearError();
  return true;
}

// ---------------------------------------------------------------------------
// EspBleHidMouse
// ---------------------------------------------------------------------------

bool EspBleHidMouse::configure(const EspBleHidMouseConfig &config)
{
  if (config.buttons < 1 || config.buttons > 8)
  {
    owner_->setError(EspBleError::InvalidArgument,
      "mouse button count must be between 1 and 8");
    return false;
  }
  auto &keyboard = owner_->hidKeyboard();
  // The button count is patched into the descriptor, so it has to be known before
  // the Report Map is composed.
  if (keyboard.impl_ != nullptr) keyboard.impl_->mouseButtonCount = config.buttons;
  if (!keyboard.configureProfile(ESP_BLE_HID_REPORT_ID_MOUSE, config))
  {
    return false;
  }
  keyboard.impl_->mouseButtonCount = config.buttons;
  if (!keyboard.impl_->recomposeReportMap()) return false;
  configured_ = true;
  return true;
}

bool EspBleHidMouse::configured() const { return configured_; }

bool EspBleHidMouse::ready() const
{
  return owner_->hidKeyboard().readyFor(ESP_BLE_HID_REPORT_ID_MOUSE);
}

bool EspBleHidMouse::sendReport(const EspBleHidMouseReport &report)
{
  const uint8_t value[4] = {report.buttons,
    static_cast<uint8_t>(report.x), static_cast<uint8_t>(report.y),
    static_cast<uint8_t>(report.wheel)};
  buttons_ = report.buttons;
  return owner_->hidKeyboard().sendRawReport(
    ESP_BLE_HID_REPORT_ID_MOUSE, value, sizeof(value));
}

bool EspBleHidMouse::move(int8_t x, int8_t y, int8_t wheel, uint8_t buttons)
{
  EspBleHidMouseReport report;
  // Movement keeps whatever buttons are held, unless the caller names some: a drag
  // is a move with the button still down.
  report.buttons = buttons != 0 ? buttons : buttons_;
  report.x = x;
  report.y = y;
  report.wheel = wheel;
  return sendReport(report);
}

bool EspBleHidMouse::wheel(int8_t amount) { return move(0, 0, amount, buttons_); }

bool EspBleHidMouse::press(uint8_t buttons)
{
  EspBleHidMouseReport report;
  report.buttons = static_cast<uint8_t>(buttons_ | buttons);
  return sendReport(report);
}

bool EspBleHidMouse::release(uint8_t buttons)
{
  EspBleHidMouseReport report;
  report.buttons = static_cast<uint8_t>(buttons_ & ~buttons);
  return sendReport(report);
}

bool EspBleHidMouse::click(uint8_t button, uint32_t holdMs)
{
  if (!press(button)) return false;
  delay(holdMs);
  return release(button);
}

bool EspBleHidMouse::releaseAll() { return sendReport(EspBleHidMouseReport()); }

uint8_t EspBleHidMouse::buttons() const { return buttons_; }

// ---------------------------------------------------------------------------
// EspBleHidConsumerControl — one 16-bit usage per report (volume, transport keys)
// ---------------------------------------------------------------------------

bool EspBleHidConsumerControl::configure(
  const EspBleHidConsumerControlConfig &config)
{
  if (!owner_->hidKeyboard().configureProfile(
        ESP_BLE_HID_REPORT_ID_CONSUMER_CONTROL, config))
  {
    return false;
  }
  configured_ = true;
  return true;
}

bool EspBleHidConsumerControl::configured() const { return configured_; }

bool EspBleHidConsumerControl::ready() const
{
  return owner_->hidKeyboard().readyFor(ESP_BLE_HID_REPORT_ID_CONSUMER_CONTROL);
}

bool EspBleHidConsumerControl::sendReport(uint16_t usage)
{
  const uint8_t value[2] = {static_cast<uint8_t>(usage),
    static_cast<uint8_t>(usage >> 8)};
  usage_ = usage;
  return owner_->hidKeyboard().sendRawReport(
    ESP_BLE_HID_REPORT_ID_CONSUMER_CONTROL, value, sizeof(value));
}

bool EspBleHidConsumerControl::sendUsage(uint16_t usage)
{
  return sendReport(usage);
}

bool EspBleHidConsumerControl::press(uint16_t usage) { return sendReport(usage); }

// Usage 0 is the released state: this report carries one usage at a time, so
// there is nothing to subtract.
bool EspBleHidConsumerControl::release() { return sendReport(0); }

bool EspBleHidConsumerControl::click(uint16_t usage, uint32_t holdMs)
{
  if (!press(usage)) return false;
  delay(holdMs);
  return release();
}

bool EspBleHidConsumerControl::releaseAll() { return release(); }

uint16_t EspBleHidConsumerControl::usage() const { return usage_; }

// ---------------------------------------------------------------------------
// EspBleHidSystemControl — power down, sleep, wake up
// ---------------------------------------------------------------------------

bool EspBleHidSystemControl::configure(const EspBleHidSystemControlConfig &config)
{
  if (!owner_->hidKeyboard().configureProfile(
        ESP_BLE_HID_REPORT_ID_SYSTEM_CONTROL, config))
  {
    return false;
  }
  configured_ = true;
  return true;
}

bool EspBleHidSystemControl::configured() const { return configured_; }

bool EspBleHidSystemControl::ready() const
{
  return owner_->hidKeyboard().readyFor(ESP_BLE_HID_REPORT_ID_SYSTEM_CONTROL);
}

bool EspBleHidSystemControl::sendReport(uint8_t usage)
{
  usage_ = usage;
  return owner_->hidKeyboard().sendRawReport(
    ESP_BLE_HID_REPORT_ID_SYSTEM_CONTROL, &usage, 1);
}

bool EspBleHidSystemControl::sendUsage(uint8_t usage) { return sendReport(usage); }

bool EspBleHidSystemControl::press(uint8_t usage) { return sendReport(usage); }

bool EspBleHidSystemControl::release() { return sendReport(0); }

bool EspBleHidSystemControl::click(uint8_t usage, uint32_t holdMs)
{
  if (!press(usage)) return false;
  delay(holdMs);
  return release();
}

bool EspBleHidSystemControl::releaseAll() { return release(); }

uint8_t EspBleHidSystemControl::usage() const { return usage_; }

// ---------------------------------------------------------------------------
// EspBleHidGamepad — six signed axes, a hat switch and 32 buttons
// ---------------------------------------------------------------------------

bool EspBleHidGamepad::configure(const EspBleHidGamepadConfig &config)
{
  if (!owner_->hidKeyboard().configureProfile(
        ESP_BLE_HID_REPORT_ID_GAMEPAD, config))
  {
    return false;
  }
  configured_ = true;
  return true;
}

bool EspBleHidGamepad::configured() const { return configured_; }

bool EspBleHidGamepad::ready() const
{
  return owner_->hidKeyboard().readyFor(ESP_BLE_HID_REPORT_ID_GAMEPAD);
}

bool EspBleHidGamepad::sendReport(const EspBleHidGamepadReport &report)
{
  // The descriptor's order: X, Y, Z, Rz, Rx, Ry, hat, then 32 button bits.
  const uint8_t value[11] = {
    static_cast<uint8_t>(report.x), static_cast<uint8_t>(report.y),
    static_cast<uint8_t>(report.z), static_cast<uint8_t>(report.rz),
    static_cast<uint8_t>(report.rx), static_cast<uint8_t>(report.ry),
    report.hat,
    static_cast<uint8_t>(report.buttons),
    static_cast<uint8_t>(report.buttons >> 8),
    static_cast<uint8_t>(report.buttons >> 16),
    static_cast<uint8_t>(report.buttons >> 24)};
  return owner_->hidKeyboard().sendRawReport(
    ESP_BLE_HID_REPORT_ID_GAMEPAD, value, sizeof(value));
}

bool EspBleHidGamepad::send(int8_t x, int8_t y, int8_t z, int8_t rz, int8_t rx,
  int8_t ry, uint8_t hat, uint32_t buttons)
{
  EspBleHidGamepadReport report;
  report.x = x;
  report.y = y;
  report.z = z;
  report.rz = rz;
  report.rx = rx;
  report.ry = ry;
  report.hat = hat;
  report.buttons = buttons;
  return sendReport(report);
}

bool EspBleHidGamepad::releaseAll()
{
  return sendReport(EspBleHidGamepadReport());
}

// ---------------------------------------------------------------------------
// EspBleHidVendor — one Input, one Output and one Feature report of a
// caller-chosen size, with bytes the library does not interpret
// ---------------------------------------------------------------------------

bool EspBleHidKeyboard::configureVendorReports()
{
  if (impl_ == nullptr || !impl_->configured) return false;
  if (impl_->vendorOutput.valid()) return true;  // already registered

  auto &server = owner_->gattServer();
  EspBleGattCharacteristicConfig outputConfig;
  outputConfig.readable = true;
  outputConfig.writable = true;
  outputConfig.writableWithoutResponse = true;
  // A Feature report is configuration rather than state, so it is written with a
  // response only — the same distinction the Report Descriptor draws.
  EspBleGattCharacteristicConfig featureConfig;
  featureConfig.readable = true;
  featureConfig.writable = true;

  impl_->vendorOutput = server.addCharacteristic(
    impl_->hidService, ReportUuid, outputConfig);
  impl_->vendorOutputReference =
    server.addDescriptor(impl_->vendorOutput, ReportReferenceUuid);
  impl_->vendorFeature = server.addCharacteristic(
    impl_->hidService, ReportUuid, featureConfig);
  impl_->vendorFeatureReference =
    server.addDescriptor(impl_->vendorFeature, ReportReferenceUuid);
  if (!impl_->vendorOutput || !impl_->vendorOutputReference ||
      !impl_->vendorFeature || !impl_->vendorFeatureReference)
  {
    return false;
  }

  const uint8_t outputReference[2] =
    {ESP_BLE_HID_REPORT_ID_VENDOR, ESP_BLE_HID_REPORT_TYPE_OUTPUT};
  const uint8_t featureReference[2] =
    {ESP_BLE_HID_REPORT_ID_VENDOR, ESP_BLE_HID_REPORT_TYPE_FEATURE};
  uint8_t empty[EspBleHidDeviceManagerImpl::MaxVendorReportSize] = {};
  const size_t length = impl_->vendorReportSize;
  return server.setDescriptorValue(
      impl_->vendorOutputReference, outputReference, sizeof(outputReference)) &&
    server.setDescriptorValue(
      impl_->vendorFeatureReference, featureReference,
      sizeof(featureReference)) &&
    server.setValue(
      impl_->inputReports[ESP_BLE_HID_REPORT_ID_VENDOR - 1], empty, length) &&
    server.setValue(impl_->vendorOutput, empty, length) &&
    server.setValue(impl_->vendorFeature, empty, length);
}

bool EspBleHidVendor::configure(const EspBleHidVendorConfig &config)
{
  if (config.reportSize == 0 ||
      config.reportSize > EspBleHidDeviceManagerImpl::MaxVendorReportSize)
  {
    owner_->setError(EspBleError::InvalidArgument,
      "vendor HID report size must be between 1 and 64");
    return false;
  }
  auto &keyboard = owner_->hidKeyboard();
  // The size is patched into the descriptor, so it has to be known before the
  // Report Map is composed.
  if (keyboard.impl_ != nullptr) keyboard.impl_->vendorReportSize = config.reportSize;
  if (!keyboard.configureProfile(ESP_BLE_HID_REPORT_ID_VENDOR, config))
  {
    return false;
  }
  keyboard.impl_->vendorReportSize = config.reportSize;
  if (!keyboard.impl_->recomposeReportMap() || !keyboard.configureVendorReports())
  {
    return false;
  }
  configured_ = true;
  return true;
}

bool EspBleHidVendor::configured() const { return configured_; }

bool EspBleHidVendor::ready() const
{
  return owner_->hidKeyboard().readyFor(ESP_BLE_HID_REPORT_ID_VENDOR);
}

bool EspBleHidVendor::sendInput(const void *data, size_t length)
{
  auto &keyboard = owner_->hidKeyboard();
  if (!configured_ || keyboard.impl_ == nullptr || data == nullptr ||
      length == 0 || length != keyboard.impl_->vendorReportSize)
  {
    // The descriptor declares one fixed size, so a short report is a mismatch
    // rather than a partial one.
    owner_->setError(EspBleError::InvalidArgument,
      "invalid vendor HID input report");
    return false;
  }
  return keyboard.sendRawReport(ESP_BLE_HID_REPORT_ID_VENDOR,
    static_cast<const uint8_t *>(data), length);
}

void EspBleHidVendor::onOutputReport(ReportCallback callback)
{
  outputCallback_ = std::move(callback);
}

void EspBleHidVendor::onFeatureReport(ReportCallback callback)
{
  featureCallback_ = std::move(callback);
}

// ---------------------------------------------------------------------------
// EspBleHidCustom — an arbitrary Report Descriptor with caller-declared reports
// ---------------------------------------------------------------------------

bool EspBleHidKeyboard::configureCustom(const EspBleHidDeviceConfig &config)
{
  if (!configureCommon(config)) return false;
  impl_->customConfigured = true;
  return true;
}

bool EspBleHidKeyboard::registerCustomReport(size_t slot)
{
  EspBleHidDeviceManagerImpl::CustomReport &report = impl_->customReports[slot];
  auto &server = owner_->gattServer();
  EspBleGattCharacteristicConfig characteristicConfig;
  characteristicConfig.readable = true;
  if (report.reportType == ESP_BLE_HID_REPORT_TYPE_INPUT)
  {
    characteristicConfig.notifiable = true;
  }
  else
  {
    characteristicConfig.writable = true;
    // Only an Output report takes Write Without Response: a Feature report is
    // configuration, so the host wants the acknowledgement.
    characteristicConfig.writableWithoutResponse =
      report.reportType == ESP_BLE_HID_REPORT_TYPE_OUTPUT;
  }
  report.characteristic =
    server.addCharacteristic(impl_->hidService, ReportUuid, characteristicConfig);
  report.reference = server.addDescriptor(report.characteristic, ReportReferenceUuid);
  if (!report.characteristic || !report.reference) return false;

  const uint8_t reference[2] = {report.reportId, report.reportType};
  uint8_t empty[EspBleHidDeviceManagerImpl::MaxVendorReportSize] = {};
  return server.setDescriptorValue(report.reference, reference, sizeof(reference)) &&
    server.setValue(report.characteristic, empty, report.size);
}

int EspBleHidKeyboard::customInputSlot(uint8_t reportId) const
{
  if (impl_ == nullptr) return -1;
  for (size_t index = 0; index < impl_->customReportCount; ++index)
  {
    if (impl_->customReports[index].reportType == ESP_BLE_HID_REPORT_TYPE_INPUT &&
        impl_->customReports[index].reportId == reportId)
    {
      return static_cast<int>(index);
    }
  }
  return -1;
}

bool EspBleHidKeyboard::readyForCustom(uint8_t reportId) const
{
  const int slot = customInputSlot(reportId);
  if (slot < 0 || impl_ == nullptr || !impl_->realized || !owner_->initialized())
  {
    return false;
  }
  EspBleConnection connection;
  if (!impl_->peripheralConnection(connection)) return false;
  if (impl_->securityEnabled && !connection.encrypted) return false;
  const EspBleHidDeviceManagerImpl::SubscriptionSlot *subscriber =
    impl_->slotFor(connection.id, false);
  if (subscriber == nullptr) return false;
  return (subscriber->customNotifications &
    static_cast<uint8_t>(1u << slot)) != 0;
}

bool EspBleHidKeyboard::sendCustomInput(
  uint8_t reportId, const uint8_t *data, size_t length)
{
  if (impl_ == nullptr || !impl_->configured || !impl_->realized ||
      !owner_->initialized())
  {
    owner_->setError(EspBleError::InvalidState,
      "HID Custom Device is not initialized");
    return false;
  }
  const int slot = customInputSlot(reportId);
  if (slot < 0)
  {
    owner_->setError(EspBleError::NotFound, "unknown custom HID input report");
    return false;
  }
  const EspBleHidDeviceManagerImpl::CustomReport &report =
    impl_->customReports[slot];
  if (data == nullptr || length == 0 || length != report.size)
  {
    owner_->setError(EspBleError::InvalidArgument,
      "invalid custom HID input report length");
    return false;
  }

  EspBleConnection connection;
  const bool anyPeripheral = impl_->peripheralConnection(connection);
  bool sent = false;
  if (anyPeripheral && (!impl_->securityEnabled || connection.encrypted))
  {
    const EspBleHidDeviceManagerImpl::SubscriptionSlot *subscriber =
      impl_->slotFor(connection.id, false);
    if (subscriber != nullptr &&
        (subscriber->customNotifications &
          static_cast<uint8_t>(1u << slot)) != 0)
    {
      sent = owner_->gattServer().notify(
        connection.id, report.characteristic, data, length);
    }
  }
  if (!sent)
  {
    owner_->setError(EspBleError::InvalidState,
      anyPeripheral ? "no subscribed HID Host" : "no connected HID Host");
    return false;
  }
  owner_->clearError();
  return true;
}

bool EspBleHidCustom::configure(const EspBleHidDeviceConfig &config)
{
  configured_ = owner_->hidKeyboard().configureCustom(config);
  return configured_;
}

bool EspBleHidCustom::configured() const { return configured_; }

bool EspBleHidCustom::ready(uint8_t reportId) const
{
  return owner_->hidKeyboard().readyForCustom(reportId);
}

bool EspBleHidCustom::setReportMap(const uint8_t *descriptor, size_t length)
{
  EspBleHidDeviceManagerImpl *impl = owner_->hidKeyboard().impl_;
  if (!configured_ || impl == nullptr)
  {
    owner_->setError(EspBleError::InvalidState,
      "call hidCustom().configure() first");
    return false;
  }
  if (descriptor == nullptr || length == 0 ||
      length > EspBleHidDeviceManagerImpl::CustomReportMapCapacity)
  {
    owner_->setError(EspBleError::InvalidArgument,
      "invalid custom HID report descriptor");
    return false;
  }
  memcpy(impl->customReportMap, descriptor, length);
  impl->customReportMapLength = length;
  // The Report Map is the composed profiles followed by this descriptor, so it
  // has to be rebuilt rather than replaced.
  if (!impl->recomposeReportMap()) return false;
  owner_->clearError();
  return true;
}

bool EspBleHidCustom::addReport(
  uint8_t reportId, uint8_t reportType, uint16_t sizeBytes)
{
  auto &keyboard = owner_->hidKeyboard();
  EspBleHidDeviceManagerImpl *impl = keyboard.impl_;
  if (!configured_ || impl == nullptr)
  {
    owner_->setError(EspBleError::InvalidState,
      "call hidCustom().configure() first");
    return false;
  }
  if (reportId == 0 || sizeBytes == 0 ||
      sizeBytes > EspBleHidDeviceManagerImpl::MaxVendorReportSize)
  {
    owner_->setError(EspBleError::InvalidArgument,
      "invalid custom HID report id or size");
    return false;
  }
  // Report IDs 1..6 are reserved for the built-in profiles when one is enabled.
  if (reportId <= hid::ProfileCount &&
      (impl->profileMask & static_cast<uint8_t>(1u << (reportId - 1))) != 0)
  {
    owner_->setError(EspBleError::InvalidArgument,
      "custom HID report id conflicts with an enabled built-in profile");
    return false;
  }
  for (size_t index = 0; index < impl->customReportCount; ++index)
  {
    if (impl->customReports[index].reportId == reportId &&
        impl->customReports[index].reportType == reportType)
    {
      owner_->setError(EspBleError::InvalidArgument, "duplicate custom HID report");
      return false;
    }
  }
  if (impl->customReportCount == EspBleHidDeviceManagerImpl::MaxCustomReports)
  {
    owner_->setError(EspBleError::ResourceExhausted, "too many custom HID reports");
    return false;
  }
  const size_t slot = impl->customReportCount;
  EspBleHidDeviceManagerImpl::CustomReport &report = impl->customReports[slot];
  report.reportId = reportId;
  report.reportType = reportType;
  report.size = sizeBytes;
  ++impl->customReportCount;
  if (!keyboard.registerCustomReport(slot))
  {
    // Registration failed, so the slot must not stay claimed: the subscription
    // bit and the write dispatch are both indexed by it.
    report = EspBleHidDeviceManagerImpl::CustomReport();
    --impl->customReportCount;
    return false;
  }
  owner_->clearError();
  return true;
}

bool EspBleHidCustom::addInputReport(uint8_t reportId, uint16_t sizeBytes)
{
  return addReport(reportId, ESP_BLE_HID_REPORT_TYPE_INPUT, sizeBytes);
}

bool EspBleHidCustom::addOutputReport(uint8_t reportId, uint16_t sizeBytes)
{
  return addReport(reportId, ESP_BLE_HID_REPORT_TYPE_OUTPUT, sizeBytes);
}

bool EspBleHidCustom::addFeatureReport(uint8_t reportId, uint16_t sizeBytes)
{
  return addReport(reportId, ESP_BLE_HID_REPORT_TYPE_FEATURE, sizeBytes);
}

bool EspBleHidCustom::sendInput(
  uint8_t reportId, const uint8_t *data, size_t length)
{
  return owner_->hidKeyboard().sendCustomInput(reportId, data, length);
}

void EspBleHidCustom::onOutputReport(ReportCallback callback)
{
  outputCallback_ = std::move(callback);
}

void EspBleHidCustom::onFeatureReport(ReportCallback callback)
{
  featureCallback_ = std::move(callback);
}
