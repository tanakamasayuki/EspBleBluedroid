#ifndef ESP_BLE_BLUEDROID_H
#define ESP_BLE_BLUEDROID_H

#include <Arduino.h>
#include <functional>
#include <memory>
#include <mutex>
#include <sdkconfig.h>

#if !defined(CONFIG_BLUEDROID_ENABLED) || defined(CONFIG_NIMBLE_ENABLED)
#error "EspBleBluedroid requires the Bluedroid backend bundled with Arduino-ESP32"
#endif

#include "EspBleKeymap.h"
#include "espblebluedroid_version.h"

enum class EspBleError : uint8_t
{
  None = 0,
  InvalidState,
  InvalidArgument,
  BackendFailure,
  ResourceExhausted,
  NotFound,
  Timeout,
  Unsupported,
};

enum class EspBleSecurityIoCapability : uint8_t
{
  None = 0,
  DisplayOnly,
  KeyboardOnly,
  DisplayYesNo,
};

// Which address Legacy Advertising presents to BLE peers.
enum class EspBleOwnAddressType : uint8_t
{
  Public = 0,
  RandomStatic,
  ResolvablePrivate,
};

struct EspBleSecurityConfig
{
  bool enabled = false;
  bool bonding = true;
  bool pairOnConnect = true;
  bool mitm = false;
  EspBleSecurityIoCapability ioCapability = EspBleSecurityIoCapability::None;
  bool staticPasskeyEnabled = false;
  uint32_t staticPasskey = 0;
};

enum class EspBluedroidClassicSecurityIoCapability : uint8_t
{
  None = 0,
  DisplayOnly,
  KeyboardOnly,
  DisplayYesNo,
};

struct EspBluedroidClassicSecurityConfig
{
  bool enabled = false;
  EspBluedroidClassicSecurityIoCapability ioCapability =
    EspBluedroidClassicSecurityIoCapability::None;
  uint32_t responseTimeoutMilliseconds = 30000;
};

struct EspBluedroidClassicBond
{
  String peerAddress;
};

struct EspBleConfig
{
  const char *deviceName = "EspBleBluedroid";
  uint16_t preferredMtu = 247;
  EspBleSecurityConfig security;
  EspBluedroidClassicSecurityConfig classicSecurity;
  // Advertising address privacy. RandomStatic hides the factory public address
  // with one generated identity. ResolvablePrivate enables controller-managed
  // RPA privacy and is useful with bonding so peers resolve address rotation.
  EspBleOwnAddressType ownAddressType = EspBleOwnAddressType::Public;
};

struct EspBleScanConfig
{
  bool active = true;
  bool wantDuplicates = false;
  uint16_t intervalMilliseconds = 100;
  uint16_t windowMilliseconds = 50;
  uint32_t durationSeconds = 0;
  // Report only advertisers on the Filter Accept List.
  bool acceptListOnly = false;
};

enum class EspBleAddressType : uint8_t
{
  Public = 0,
  Random,
  PublicIdentity,
  RandomIdentity,
};

struct EspBleServiceData
{
  String uuid;
  String data;
};

struct EspBleScanResult
{
  static constexpr size_t MaxServiceUuids = 8;
  static constexpr size_t MaxServiceData = 4;

  String address;
  EspBleAddressType addressType = EspBleAddressType::Public;
  String name;
  int rssi = 0;
  bool connectable = false;
  bool scannable = false;
  String manufacturerData;
  EspBleServiceData serviceData[MaxServiceData];
  size_t serviceDataCount = 0;
  String serviceUuids[MaxServiceUuids];
  size_t serviceUuidCount = 0;
  uint16_t appearance = 0;
  int8_t txPowerLevel = 0;
  bool txPowerLevelPresent = false;

  bool hasName() const;
  bool hasManufacturerData() const;
  bool hasServiceData() const;
  bool hasAppearance() const;
  bool hasTxPowerLevel() const;
  bool serviceDataFor(const char *uuid, String &data) const;
  bool advertisesService(const char *uuid) const;
};

enum class EspBleRole : uint8_t
{
  Central = 0,
  Peripheral,
};

using EspBleConnectionId = uint32_t;

// Everything from here to the end of EspBleCallbackList is a verbatim copy of
// EspBle's, so a profile helper written against one library compiles against the
// other.
using EspBleListenerId = uint32_t;
constexpr EspBleListenerId EspBleInvalidListenerId = 0;

// Multi-observer slot for one event: a single "primary" callback (set via the
// on*() setters, kept for the common single-observer case and backward
// compatibility) plus up to MaxListeners additional listeners (add*Listener()).
// The owner serializes every call with its own mutex; this type does no locking
// itself. Dispatch takes a snapshot under the lock and invokes it unlocked, so a
// callback may add/remove listeners without deadlocking or being invoked in the
// same dispatch. Removal shifts later slots down; listener ids are owner-unique.
template <typename Callback, size_t MaxListeners = 4>
class EspBleCallbackList
{
public:
  static constexpr size_t Capacity = MaxListeners + 1; // + primary

  void setPrimary(Callback callback)
  {
    primary_ = callback ? std::make_shared<Callback>(std::move(callback)) : nullptr;
  }

  // Store callback under listenerId (allocated by the owner). Returns listenerId
  // on success or EspBleInvalidListenerId if the list is full / callback empty.
  EspBleListenerId add(Callback callback, EspBleListenerId listenerId)
  {
    if (!callback || listenerId == EspBleInvalidListenerId) return EspBleInvalidListenerId;
    for (size_t i = 0; i < MaxListeners; ++i)
    {
      if (listeners_[i].id == EspBleInvalidListenerId)
      {
        listeners_[i].id = listenerId;
        listeners_[i].callback = std::make_shared<Callback>(std::move(callback));
        return listenerId;
      }
    }
    return EspBleInvalidListenerId;
  }

  bool remove(EspBleListenerId listenerId)
  {
    for (size_t i = 0; i < MaxListeners; ++i)
    {
      if (listeners_[i].id == listenerId)
      {
        for (size_t next = i + 1; next < MaxListeners; ++next)
        {
          listeners_[next - 1] = std::move(listeners_[next]);
        }
        listeners_[MaxListeners - 1] = Slot();
        return true;
      }
    }
    return false;
  }

  bool contains(EspBleListenerId listenerId) const
  {
    for (const Slot &slot : listeners_)
    {
      if (slot.id == listenerId) return true;
    }
    return false;
  }

  // Copy the primary (first) then each listener into out[], returning the count.
  // out must hold at least Capacity entries.
  size_t snapshot(std::shared_ptr<Callback> *out) const
  {
    size_t count = 0;
    if (primary_) out[count++] = primary_;
    for (const Slot &slot : listeners_)
    {
      if (slot.callback) out[count++] = slot.callback;
    }
    return count;
  }

private:
  struct Slot
  {
    EspBleListenerId id = EspBleInvalidListenerId;
    std::shared_ptr<Callback> callback;
  };
  std::shared_ptr<Callback> primary_;
  Slot listeners_[MaxListeners];
};

struct EspBleConnection
{
  EspBleConnectionId id = 0;
  uint16_t handle = 0xffff;
  String peerAddress;
  EspBleAddressType peerAddressType = EspBleAddressType::Public;
  EspBleRole localRole = EspBleRole::Central;
  uint16_t mtu = 23;
  bool encrypted = false;
  bool authenticated = false;
  bool bonded = false;
  uint8_t encryptionKeySize = 0;
  // Meaningful in onDisconnected(): the HCI disconnection reason, or 0 when
  // unavailable. It is 0 in connection-state update callbacks.
  int disconnectReason = 0;
  // Current BLE connection parameters. Interval uses 1.25 ms units, timeout
  // uses 10 ms units, and latency counts skipped connection events.
  uint16_t connectionInterval = 0;
  uint16_t peripheralLatency = 0;
  uint16_t supervisionTimeout = 0;

  size_t maximumNotificationPayload() const;
};

struct EspBleMtuChanged
{
  EspBleConnection connection;
  uint16_t previousMtu = 23;
};

struct EspBleConnectionFailure
{
  String peerAddress;
  EspBleError error = EspBleError::BackendFailure;
  String detail;
};

struct EspBleSecurityChanged
{
  EspBleConnection connection;
  bool success = false;
  EspBleError error = EspBleError::None;
  String detail;
};

struct EspBlePasskeyDisplayed
{
  EspBleConnection connection;
  uint32_t passkey = 0;
};

struct EspBleBond
{
  String peerAddress;
  EspBleAddressType peerAddressType = EspBleAddressType::Public;
};

struct EspBleGattCharacteristicConfig
{
  bool readable = false;
  bool writable = false;
  bool writableWithoutResponse = false;
  bool notifiable = false;
  bool indicatable = false;
  bool encryptedRead = false;
  bool encryptedWrite = false;
  bool authenticatedRead = false;
  bool authenticatedWrite = false;
};

struct EspBleGattDescriptorConfig
{
  bool readable = true;
  bool writable = false;
  bool encryptedRead = false;
  bool encryptedWrite = false;
  bool authenticatedRead = false;
  bool authenticatedWrite = false;
  uint16_t maximumLength = 100;
};

enum class EspBleAdvertisingFilterPolicy : uint8_t
{
  Any = 0,
  ScanRequestFromAcceptList,
  ConnectionFromAcceptList,
  Both,
};

// Advertising channels, as a bit mask for EspBleAdvertising::setChannelMap().
enum EspBleAdvertisingChannel : uint8_t
{
  EspBleAdvertisingChannel37 = 0x01,
  EspBleAdvertisingChannel38 = 0x02,
  EspBleAdvertisingChannel39 = 0x04,
  EspBleAdvertisingChannelAll = 0x07,
};

enum class EspBleGattOperation : uint8_t
{
  Discover = 0,
  Read,
  Write,
  Subscribe,
  Unsubscribe,
  DiscoverServices,
  ReadDescriptor,
  WriteDescriptor,
};

struct EspBleGattResult
{
  EspBleGattOperation operation = EspBleGattOperation::Discover;
  EspBleConnectionId connectionId = 0;
  String serviceUuid;
  String characteristicUuid;
  String descriptorUuid;
  uint16_t handle = 0;
  uint16_t descriptorHandle = 0;
  bool success = false;
  EspBleError error = EspBleError::None;
  String detail;
  String value;
  bool readable = false;
  bool writable = false;
  bool writableWithoutResponse = false;
  bool notifiable = false;
  bool indicatable = false;
  bool subscribedToNotifications = false;
  bool subscribedToIndications = false;
  bool response = true;
};

struct EspBleGattNotification
{
  EspBleConnectionId connectionId = 0;
  String serviceUuid;
  String characteristicUuid;
  uint16_t handle = 0;
  String value;
  bool indication = false;
};

struct EspBleGattServiceInfo
{
  String serviceUuid;
  uint16_t handle = 0;
};

struct EspBleGattCharacteristicInfo
{
  String serviceUuid;
  String characteristicUuid;
  uint16_t handle = 0;
  bool readable = false;
  bool writable = false;
  bool writableWithoutResponse = false;
  bool notifiable = false;
  bool indicatable = false;
};

struct EspBleGattDescriptorInfo
{
  String serviceUuid;
  String characteristicUuid;
  String descriptorUuid;
  uint16_t handle = 0;
  uint16_t characteristicHandle = 0;
};

struct EspBleGattService
{
  uint16_t id = 0;
  bool valid() const { return id != 0; }
  explicit operator bool() const { return valid(); }
};

struct EspBleGattCharacteristic
{
  uint16_t id = 0;
  bool valid() const { return id != 0; }
  explicit operator bool() const { return valid(); }
  bool operator==(const EspBleGattCharacteristic &other) const
    { return id == other.id; }
  bool operator!=(const EspBleGattCharacteristic &other) const
    { return id != other.id; }
};

struct EspBleGattDescriptor
{
  uint16_t id = 0;
  bool valid() const { return id != 0; }
  explicit operator bool() const { return valid(); }
  bool operator==(const EspBleGattDescriptor &other) const
    { return id == other.id; }
  bool operator!=(const EspBleGattDescriptor &other) const
    { return id != other.id; }
};

struct EspBleGattWrite
{
  EspBleConnectionId connectionId = 0;
  EspBleGattCharacteristic characteristic;
  String serviceUuid;
  String characteristicUuid;
  String value;
};

struct EspBleGattReadRequest
{
  EspBleConnectionId connectionId = 0;
  EspBleGattCharacteristic characteristic;
  String serviceUuid;
  String characteristicUuid;
};

struct EspBleGattDescriptorWrite
{
  EspBleConnectionId connectionId = 0;
  EspBleGattDescriptor descriptor;
  String serviceUuid;
  String characteristicUuid;
  String descriptorUuid;
  String value;
};

struct EspBleGattSubscription
{
  EspBleConnectionId connectionId = 0;
  EspBleGattCharacteristic characteristic;
  String serviceUuid;
  String characteristicUuid;
  bool notifications = false;
  bool indications = false;
};

struct EspBleGattSendResult
{
  EspBleConnectionId connectionId = 0;
  EspBleGattCharacteristic characteristic;
  String serviceUuid;
  String characteristicUuid;
  String value;
  bool indication = false;
  bool success = false;
  EspBleError error = EspBleError::None;
  String detail;
};

// ---------------------------------------------------------------------------
// HID over GATT (HOGP) device profiles.
//
// The report IDs, the report structures and the class API are EspBle's, so a HID
// sketch ports across with a rename. The descriptors published for each profile
// are byte-identical too (src/internal/EspBleBluedroidHidReportMaps.h, pinned by
// tests/unit/hid_report_maps) — a host OS parses them to decide what the device
// is, so they are a wire format rather than an implementation choice.
// ---------------------------------------------------------------------------

static constexpr uint8_t ESP_BLE_HID_REPORT_ID_KEYBOARD = 0x01;
static constexpr uint8_t ESP_BLE_HID_REPORT_ID_MOUSE = 0x02;
static constexpr uint8_t ESP_BLE_HID_REPORT_ID_GAMEPAD = 0x03;
static constexpr uint8_t ESP_BLE_HID_REPORT_ID_CONSUMER_CONTROL = 0x04;
static constexpr uint8_t ESP_BLE_HID_REPORT_ID_SYSTEM_CONTROL = 0x05;
static constexpr uint8_t ESP_BLE_HID_REPORT_ID_VENDOR = 0x06;

enum EspBleHidReportType : uint8_t
{
  ESP_BLE_HID_REPORT_TYPE_INPUT = 0x01,
  ESP_BLE_HID_REPORT_TYPE_OUTPUT = 0x02,
  ESP_BLE_HID_REPORT_TYPE_FEATURE = 0x03,
};

static constexpr uint8_t ESP_BLE_HID_MOUSE_LEFT = 0x01;
static constexpr uint8_t ESP_BLE_HID_MOUSE_RIGHT = 0x02;
static constexpr uint8_t ESP_BLE_HID_MOUSE_MIDDLE = 0x04;
static constexpr uint8_t ESP_BLE_HID_MOUSE_BACK = 0x08;
static constexpr uint8_t ESP_BLE_HID_MOUSE_FORWARD = 0x10;

// Hat switch positions, in the order the gamepad descriptor declares them.
static constexpr uint8_t ESP_BLE_HID_GAMEPAD_HAT_CENTER = 0x00;
static constexpr uint8_t ESP_BLE_HID_GAMEPAD_HAT_UP = 0x01;
static constexpr uint8_t ESP_BLE_HID_GAMEPAD_HAT_UP_RIGHT = 0x02;
static constexpr uint8_t ESP_BLE_HID_GAMEPAD_HAT_RIGHT = 0x03;
static constexpr uint8_t ESP_BLE_HID_GAMEPAD_HAT_DOWN_RIGHT = 0x04;
static constexpr uint8_t ESP_BLE_HID_GAMEPAD_HAT_DOWN = 0x05;
static constexpr uint8_t ESP_BLE_HID_GAMEPAD_HAT_DOWN_LEFT = 0x06;
static constexpr uint8_t ESP_BLE_HID_GAMEPAD_HAT_LEFT = 0x07;
static constexpr uint8_t ESP_BLE_HID_GAMEPAD_HAT_UP_LEFT = 0x08;

// The Consumer page usages the consumer control descriptor covers, and the three
// System Control usages its descriptor declares. Both are HID usage-table values,
// so a sketch may pass any usage the descriptor's range allows.
static constexpr uint16_t ESP_BLE_HID_CONSUMER_CONTROL_NEXT_TRACK = 0x00b5;
static constexpr uint16_t ESP_BLE_HID_CONSUMER_CONTROL_PREVIOUS_TRACK = 0x00b6;
static constexpr uint16_t ESP_BLE_HID_CONSUMER_CONTROL_PLAY_PAUSE = 0x00cd;
static constexpr uint16_t ESP_BLE_HID_CONSUMER_CONTROL_MUTE = 0x00e2;
static constexpr uint16_t ESP_BLE_HID_CONSUMER_CONTROL_VOLUME_UP = 0x00e9;
static constexpr uint16_t ESP_BLE_HID_CONSUMER_CONTROL_VOLUME_DOWN = 0x00ea;

static constexpr uint8_t ESP_BLE_HID_SYSTEM_CONTROL_POWER_OFF = 0x01;
static constexpr uint8_t ESP_BLE_HID_SYSTEM_CONTROL_STANDBY = 0x02;
static constexpr uint8_t ESP_BLE_HID_SYSTEM_CONTROL_WAKE_HOST = 0x03;

struct EspBleHidDeviceConfig
{
  const char *manufacturer = "EspBle";
  uint16_t vendorId = 0xffff;
  uint16_t productId = 0x0001;
  uint16_t productVersion = 0x0001;
  uint8_t countryCode = 0;
  uint8_t initialBatteryLevel = 100;
};

struct EspBleHidKeyboardConfig : EspBleHidDeviceConfig
{
  EspBleKeyboardLayout layout = EspBleKeyboardLayout::EnUs;
  // Expose HID over GATT Boot Protocol (Protocol Mode 0x2A4E + Boot Keyboard
  // Input/Output Reports 0x2A22/0x2A32). Off by default: most HOGP hosts use
  // Report Protocol Mode, and the extra characteristics enlarge every host's
  // discovery. Enable only for hosts that need Boot Protocol (e.g. a BIOS).
  bool bootProtocol = false;
};

struct EspBleHidMouseConfig : EspBleHidDeviceConfig
{
  uint8_t buttons = 5;
};

struct EspBleHidConsumerControlConfig : EspBleHidDeviceConfig {};
struct EspBleHidSystemControlConfig : EspBleHidDeviceConfig {};
struct EspBleHidGamepadConfig : EspBleHidDeviceConfig {};

struct EspBleHidVendorConfig : EspBleHidDeviceConfig
{
  uint8_t reportSize = 63;
};

struct EspBleHidKeyboardInputReport
{
  static constexpr uint8_t LeftControl = 0x01;
  static constexpr uint8_t LeftShift = 0x02;
  static constexpr uint8_t LeftAlt = 0x04;
  static constexpr uint8_t LeftGui = 0x08;
  static constexpr uint8_t RightControl = 0x10;
  static constexpr uint8_t RightShift = 0x20;
  static constexpr uint8_t RightAlt = 0x40;
  static constexpr uint8_t RightGui = 0x80;

  uint8_t modifiers = 0;
  uint8_t keys[6] = {};
};

struct EspBleHidMouseReport
{
  uint8_t buttons = 0;
  int8_t x = 0;
  int8_t y = 0;
  int8_t wheel = 0;
};

struct EspBleHidGamepadReport
{
  int8_t x = 0;
  int8_t y = 0;
  int8_t z = 0;
  int8_t rz = 0;
  int8_t rx = 0;
  int8_t ry = 0;
  uint8_t hat = ESP_BLE_HID_GAMEPAD_HAT_CENTER;
  uint32_t buttons = 0;
};

using EspBleHidKeyboardReport = EspBleHidKeyboardInputReport;

// Full NKRO keyboard state in one report: modifier byte + a bitmap of usages
// 0x00-0xDF (the EspUsbDevice-compatible 29-byte layout). Modifier usages
// 0xE0-0xE7 live in `modifiers`, not the bitmap, and press() / release() route
// them there automatically.
struct EspBleHidKeyboardNkroReport
{
  static constexpr size_t BitmapSize = 28;
  static constexpr uint8_t MaxBitmapUsage = 0xdf;

  uint8_t modifiers = 0;
  // A bitmap, not a usage array.
  uint8_t bitmap[BitmapSize] = {};

  void clear()
  {
    modifiers = 0;
    for (size_t index = 0; index < BitmapSize; ++index) bitmap[index] = 0;
  }

  // Returns false when the usage is above MaxBitmapUsage and is not a modifier
  // (0xE0-0xE7), i.e. this report cannot represent it.
  bool press(uint8_t usage)
  {
    if (usage >= 0xe0 && usage <= 0xe7)
    {
      modifiers |= static_cast<uint8_t>(1u << (usage - 0xe0));
      return true;
    }
    if (usage > MaxBitmapUsage) return false;
    bitmap[usage >> 3] |= static_cast<uint8_t>(1u << (usage & 7));
    return true;
  }

  bool release(uint8_t usage)
  {
    if (usage >= 0xe0 && usage <= 0xe7)
    {
      modifiers &= static_cast<uint8_t>(~(1u << (usage - 0xe0)));
      return true;
    }
    if (usage > MaxBitmapUsage) return false;
    bitmap[usage >> 3] &= static_cast<uint8_t>(~(1u << (usage & 7)));
    return true;
  }

  bool isDown(uint8_t usage) const
  {
    if (usage >= 0xe0 && usage <= 0xe7)
    {
      return (modifiers & static_cast<uint8_t>(1u << (usage - 0xe0))) != 0;
    }
    if (usage > MaxBitmapUsage) return false;
    return (bitmap[usage >> 3] & static_cast<uint8_t>(1u << (usage & 7))) != 0;
  }
};

// The LED state a host wrote. The library fills the flags from `leds`, so the
// two never disagree.
struct EspBleHidKeyboardOutputReport
{
  EspBleConnectionId connectionId = 0;
  uint8_t leds = 0;
  bool numLock = false;
  bool capsLock = false;
  bool scrollLock = false;
  bool compose = false;
  bool kana = false;

  // Set `leds` and derive the flags from it. The single place that decides what
  // each bit means, so no caller has to keep the two in step by hand.
  void setLeds(uint8_t value)
  {
    leds = value;
    numLock = (value & 0x01) != 0;
    capsLock = (value & 0x02) != 0;
    scrollLock = (value & 0x04) != 0;
    compose = (value & 0x08) != 0;
    kana = (value & 0x10) != 0;
  }
};

// What every report a peer sent carries: which link it came from, which report it
// was, and the bytes as they arrived. The decoded views (below, and the HID Host's
// events) add their interpretation on top without hiding the raw report.
struct EspBleHidReport
{
  EspBleConnectionId connectionId = 0;
  uint8_t reportId = 0;
  const uint8_t *rawData = nullptr;
  size_t rawLength = 0;
};

// An Output or Feature report a HID Host wrote, for the profiles whose payload the
// library does not interpret: hidVendor() and hidCustom(). `data` / `length` are
// the same bytes as `rawData` / `rawLength` — both spellings exist because the
// decoded profiles use the first pair for their interpretation.
struct EspBleHidVendorReport : EspBleHidReport
{
  uint8_t reportType = 0;
  const uint8_t *data = nullptr;
  size_t length = 0;
};

// ---------------------------------------------------------------------------
// HID Host: what a device's HID service turned out to be, and the reports it
// sends, decoded per profile. The structures are EspBle's.
// ---------------------------------------------------------------------------

struct EspBleHidKeyboardHostDiscovery
{
  EspBleConnectionId connectionId = 0;
  uint8_t reportId = 0;
  bool hasCountryCode = false;
  uint8_t countryCode = 0;
  bool hasOutputReport = false;
  bool hasBatteryLevel = false;
  uint8_t batteryLevel = 0;
  bool success = false;
  EspBleError error = EspBleError::None;
  String detail;
};

struct EspBleHidKeyboardState
{
  static constexpr size_t BitmapSize = 32;

  EspBleConnectionId connectionId = 0;
  uint8_t reportId = 0;
  // A bitmap, not a usage array: bit (usage & 7) of byte (usage >> 3) is the
  // state of that usage. The 6KRO EspBleHidKeyboardInputReport::keys[6] holds
  // usages instead, which is why the two carry different names.
  uint8_t bitmap[BitmapSize] = {};
  uint8_t changedBitmap[BitmapSize] = {};
  uint8_t modifiers = 0;
  bool numLock = false;
  bool capsLock = false;
  bool scrollLock = false;
  bool compose = false;
  bool kana = false;

  bool isDown(uint8_t usage) const
  {
    return (bitmap[usage >> 3] & static_cast<uint8_t>(1u << (usage & 7))) != 0;
  }
  bool wasPressed(uint8_t usage) const
  {
    return isDown(usage) &&
      (changedBitmap[usage >> 3] & static_cast<uint8_t>(1u << (usage & 7))) != 0;
  }
  bool wasReleased(uint8_t usage) const
  {
    return !isDown(usage) &&
      (changedBitmap[usage >> 3] & static_cast<uint8_t>(1u << (usage & 7))) != 0;
  }
};

struct EspBleHidKeyboardEvent : EspBleHidReport
{
  uint8_t usage = 0;
  // Unicode code point produced by the selected layout (0 when the usage
  // produces no character). `ascii` is its ISO-8859-1 subset: the low byte
  // when the code point fits in 8 bits, otherwise 0.
  uint16_t unicode = 0;
  uint8_t ascii = 0;
  uint8_t modifiers = 0;
  bool pressed = false;
  bool released = false;
  bool numLock = false;
  bool capsLock = false;
  bool scrollLock = false;
  bool compose = false;
  bool kana = false;
};

struct EspBleHidMouseEvent : EspBleHidReport
{
  int16_t x = 0;
  int16_t y = 0;
  int16_t wheel = 0;
  uint8_t buttons = 0;
  uint8_t previousButtons = 0;
  bool moved = false;
  bool buttonsChanged = false;
};

struct EspBleHidConsumerControlEvent : EspBleHidReport
{
  uint16_t usage = 0;
  bool pressed = false;
  bool released = false;
};

struct EspBleHidSystemControlEvent : EspBleHidReport
{
  uint8_t usage = 0;
  bool pressed = false;
  bool released = false;
};

struct EspBleHidFieldValue
{
  uint8_t reportId = 0;
  uint16_t usagePage = 0;
  uint16_t usage = 0;
  int32_t value = 0;
  int32_t logicalMin = 0;
  int32_t logicalMax = 0;
  uint16_t bitOffset = 0;
  uint8_t bitSize = 0;
  uint8_t flags = 0;
};

struct EspBleHidGamepadEvent : EspBleHidReport
{
  const EspBleHidFieldValue *fields = nullptr;
  size_t fieldCount = 0;
  bool changed = false;
};

struct EspBleHidVendorInputEvent : EspBleHidReport {};

struct EspBluedroidCapabilities
{
  bool ble = true;
  bool classic = false;
  bool dualMode = false;
  bool classicInquiry = false;
  bool classicSpp = false;
};

enum class EspBluedroidClassicProfile : uint8_t
{
  Gap = 0,
  Spp,
  A2dpSink,
  A2dpSource,
  AvrcpController,
  AvrcpTarget,
  HidDevice,
  HidHost,
  HfpHandsFree,
  HfpAudioGateway,
  Hsp,
  Pan,
  PbapClient,
  PbapServer,
  Map,
  Opp,
  Ftp,
  Dun,
  Sap,
  Midi,
};

enum class EspBluedroidClassicProfileStatus : uint8_t
{
  Supported = 0,
  LibraryNotImplemented,
  CoreDisabled,
  CoreApiUnavailable,
  NoStandardProfile,
};

struct EspBluedroidClassicProfileSupport
{
  EspBluedroidClassicProfile profile = EspBluedroidClassicProfile::Gap;
  EspBluedroidClassicProfileStatus status =
    EspBluedroidClassicProfileStatus::CoreApiUnavailable;
  bool coreAvailable = false;
  bool implemented = false;
  String reason;
};

struct EspBluedroidClassicInquiryConfig
{
  uint32_t durationSeconds = 10;
  uint8_t maxResponses = 0;
};

struct EspBluedroidClassicInquiryResult
{
  String address;
  String name;
  uint32_t classOfDevice = 0;
  int rssi = 0;
  bool hasClassOfDevice = false;
  bool hasRssi = false;
};

struct EspBluedroidClassicInquiryComplete
{
  bool cancelled = false;
};

using EspBluedroidSppSessionId = uint32_t;
using EspBluedroidA2dpSessionId = uint32_t;
using EspBluedroidHfpSessionId = uint32_t;

enum class EspBluedroidAvrcpCommand : uint8_t
{
  Select = 0x00,
  Up = 0x01,
  Down = 0x02,
  Left = 0x03,
  Right = 0x04,
  VolumeUp = 0x41,
  VolumeDown = 0x42,
  Mute = 0x43,
  Play = 0x44,
  Stop = 0x45,
  Pause = 0x46,
  Rewind = 0x48,
  FastForward = 0x49,
  Next = 0x4b,
  Previous = 0x4c,
};

enum class EspBluedroidAvrcpKeyState : uint8_t
{
  Pressed = 0,
  Released = 1,
};

struct EspBluedroidAvrcpConnection
{
  String peerAddress;
};

struct EspBluedroidAvrcpCommandEvent
{
  EspBluedroidAvrcpCommand command = EspBluedroidAvrcpCommand::Play;
  EspBluedroidAvrcpKeyState state = EspBluedroidAvrcpKeyState::Released;
  bool accepted = true;
};

struct EspBluedroidAvrcpVolumeEvent
{
  uint8_t volume = 0;
};

enum class EspBluedroidA2dpRole : uint8_t
{
  Sink = 0,
  Source,
};

enum class EspBluedroidA2dpStreamState : uint8_t
{
  Suspended = 0,
  Started,
};

enum class EspBluedroidA2dpCodec : uint8_t
{
  Unknown = 0,
  Sbc,
};

struct EspBluedroidA2dpCodecConfig
{
  EspBluedroidA2dpCodec codec = EspBluedroidA2dpCodec::Unknown;
  uint32_t sampleRate = 0;
  uint8_t channelCount = 0;
  uint8_t channelMode = 0;
  uint8_t blockLength = 0;
  uint8_t subbands = 0;
  uint8_t minBitpool = 0;
  uint8_t maxBitpool = 0;
};

struct EspBluedroidA2dpSession
{
  EspBluedroidA2dpSessionId id = 0;
  String peerAddress;
  EspBluedroidA2dpRole role = EspBluedroidA2dpRole::Sink;
  bool incoming = false;
  bool streaming = false;
  uint16_t audioMtu = 0;
  EspBluedroidA2dpCodecConfig codec;
};

struct EspBluedroidA2dpPcmFormat
{
  uint32_t sampleRate = 0;
  uint8_t channels = 0;
  uint8_t bytesPerSample = 2;
  uint8_t bitsPerSample = 16;
  bool interleaved = true;
};

struct EspBluedroidA2dpPcmData
{
  EspBluedroidA2dpSessionId sessionId = 0;
  EspBluedroidA2dpPcmFormat format;
  const uint8_t *data = nullptr;
  size_t length = 0;
};

struct EspBluedroidA2dpPcmRequest
{
  EspBluedroidA2dpSessionId sessionId = 0;
  EspBluedroidA2dpPcmFormat format;
  uint8_t *data = nullptr;
  size_t capacity = 0;
  size_t written = 0;
  bool flush = false;
};

struct EspBluedroidA2dpStreamChanged
{
  EspBluedroidA2dpSessionId sessionId = 0;
  EspBluedroidA2dpStreamState state =
    EspBluedroidA2dpStreamState::Suspended;
};

struct EspBluedroidA2dpStartResult
{
  EspBluedroidA2dpRole role = EspBluedroidA2dpRole::Sink;
  bool success = false;
  EspBleError error = EspBleError::None;
  String detail;
};

struct EspBluedroidA2dpConnectionFailure
{
  String peerAddress;
  EspBluedroidA2dpRole role = EspBluedroidA2dpRole::Sink;
  EspBleError error = EspBleError::BackendFailure;
  String detail;
};

enum class EspBluedroidHfpRole : uint8_t
{
  HandsFree = 0,
  AudioGateway,
};

enum class EspBluedroidHfpCodec : uint8_t
{
  Unknown = 0,
  Cvsd,
  Msbc,
};

struct EspBluedroidHfpPcmFormat
{
  uint32_t sampleRate = 0;
  uint8_t channels = 1;
  uint8_t bytesPerSample = 2;
  uint8_t bitsPerSample = 16;
  bool interleaved = true;
};

struct EspBluedroidHfpSession
{
  EspBluedroidHfpSessionId id = 0;
  String peerAddress;
  EspBluedroidHfpRole role = EspBluedroidHfpRole::HandsFree;
  bool incoming = false;
  bool audioConnected = false;
  EspBluedroidHfpCodec codec = EspBluedroidHfpCodec::Unknown;
  EspBluedroidHfpPcmFormat format;
};

struct EspBluedroidHfpAudioChanged
{
  EspBluedroidHfpSessionId sessionId = 0;
  bool connected = false;
  EspBluedroidHfpCodec codec = EspBluedroidHfpCodec::Unknown;
  EspBluedroidHfpPcmFormat format;
};

struct EspBluedroidHfpPcmData
{
  EspBluedroidHfpSessionId sessionId = 0;
  EspBluedroidHfpPcmFormat format;
  const uint8_t *data = nullptr;
  size_t length = 0;
};

struct EspBluedroidHfpPcmRequest
{
  EspBluedroidHfpSessionId sessionId = 0;
  EspBluedroidHfpPcmFormat format;
  uint8_t *data = nullptr;
  size_t capacity = 0;
  size_t written = 0;
};

struct EspBluedroidHfpStartResult
{
  EspBluedroidHfpRole role = EspBluedroidHfpRole::HandsFree;
  bool success = false;
  EspBleError error = EspBleError::None;
  String detail;
};

struct EspBluedroidHfpConnectionFailure
{
  String peerAddress;
  EspBluedroidHfpRole role = EspBluedroidHfpRole::HandsFree;
  EspBleError error = EspBleError::BackendFailure;
  String detail;
};

enum class EspBluedroidSppSecurity : uint8_t
{
  None = 0,
  Authenticate,
  AuthenticatedEncrypted,
};

struct EspBluedroidSppServerConfig
{
  const char *serviceName = "EspBleBluedroid SPP";
  uint8_t channel = 0;
  EspBluedroidSppSecurity security = EspBluedroidSppSecurity::None;
};

struct EspBluedroidSppSession
{
  EspBluedroidSppSessionId id = 0;
  String peerAddress;
  bool incoming = false;
  bool authenticated = false;
  bool encrypted = false;
};

struct EspBluedroidClassicSecurityChanged
{
  String peerAddress;
  bool success = false;
  int status = 0;
};

struct EspBluedroidClassicNumericComparison
{
  String peerAddress;
  uint32_t value = 0;
};

struct EspBluedroidClassicPasskeyDisplayed
{
  String peerAddress;
  uint32_t passkey = 0;
};

struct EspBluedroidClassicPasskeyRequested
{
  String peerAddress;
};

struct EspBluedroidSppData
{
  EspBluedroidSppSessionId sessionId = 0;
  String value;
};

struct EspBluedroidSppWriteResult
{
  EspBluedroidSppSessionId sessionId = 0;
  size_t length = 0;
  bool success = false;
  EspBleError error = EspBleError::None;
  String detail;
};

struct EspBluedroidSppConnectionFailure
{
  String peerAddress;
  EspBleError error = EspBleError::BackendFailure;
  String detail;
};

class EspBleBluedroid;
class EspBleHidKeyboard;
class EspBleHidVendor;
class EspBleHidCustom;
class EspBleHidHost;
struct EspBleHidDeviceManagerImpl;
struct EspBleHidKeyboardHostImpl;
struct EspBleScannerImpl;
struct EspBleConnectionImpl;
struct EspBleGattServerImpl;
struct EspBluedroidClassicInquiryImpl;
struct EspBluedroidSppImpl;
struct EspBluedroidA2dpImpl;
struct EspBluedroidAvrcpControllerImpl;
struct EspBluedroidAvrcpTargetImpl;
struct EspBluedroidHfpImpl;
struct EspBluedroidClassicImpl;

class EspBleAdvertisingData
{
public:
  static constexpr size_t MaxServiceUuids = 4;
  static constexpr size_t MaxServiceData = 4;

  void clear();
  void setName(const char *name);
  bool addServiceUuid(const char *uuid);
  void setManufacturerData(const uint8_t *data, size_t length);
  bool addServiceData(const char *uuid, const uint8_t *data, size_t length);
  void setAppearance(uint16_t appearance);
  void setTxPowerIncluded(bool included);
  bool isEmpty() const;

private:
  friend class EspBleAdvertising;

  String name_;
  String manufacturerData_;
  EspBleServiceData serviceData_[MaxServiceData];
  size_t serviceDataCount_ = 0;
  String serviceUuids_[MaxServiceUuids];
  size_t serviceUuidCount_ = 0;
  uint16_t appearance_ = 0;
  bool txPowerIncluded_ = false;
};

class EspBleAdvertising
{
public:
  static constexpr size_t MaxServiceUuids =
    EspBleAdvertisingData::MaxServiceUuids;

  void clear();
  EspBleAdvertisingData &data();
  EspBleAdvertisingData &scanResponse();

  void setName(const char *name);
  bool addServiceUuid(const char *uuid);
  void setManufacturerData(const uint8_t *data, size_t length);
  bool addServiceData(
    const char *uuid, const uint8_t *data, size_t length);
  void setAppearance(uint16_t appearance);
  void setScanResponseEnabled(bool enabled);
  void setFilterPolicy(EspBleAdvertisingFilterPolicy policy);
  EspBleAdvertisingFilterPolicy filterPolicy() const;
  void setConnectable(bool connectable);
  bool setInterval(uint16_t minMilliseconds, uint16_t maxMilliseconds);
  // Address one known peer instead of broadcasting to everyone. Directed
  // advertising carries no payload, so configured data is ignored until
  // clearDirectedTarget() returns this object to normal advertising.
  bool setDirectedTarget(
    const char *address,
    EspBleAddressType addressType,
    bool highDuty = false);
  void clearDirectedTarget();
  // Zero restores all three legacy advertising channels.
  bool setChannelMap(uint8_t channelMask);
  bool start(uint32_t durationSeconds = 0);
  bool stop();
  bool isAdvertising() const;

private:
  friend class EspBleBluedroid;

  explicit EspBleAdvertising(EspBleBluedroid *owner);
  void update();
  bool applyOwnAddress();

  EspBleBluedroid *owner_;
  EspBleAdvertisingData data_;
  EspBleAdvertisingData scanResponseData_;
  bool scanResponseEnabled_ = true;
  EspBleAdvertisingFilterPolicy filterPolicy_ =
    EspBleAdvertisingFilterPolicy::Any;
  bool connectable_ = true;
  uint16_t intervalMinMs_ = 0;
  uint16_t intervalMaxMs_ = 0;
  bool directed_ = false;
  bool directedHighDuty_ = false;
  String directedAddress_;
  EspBleAddressType directedAddressType_ = EspBleAddressType::Public;
  uint8_t channelMask_ = 0;
  bool advertising_ = false;
  bool directedAdvertising_ = false;
  bool directedHighDutyCycle_ = false;
  uint32_t startedAtMs_ = 0;
  uint32_t durationMs_ = 0;
};

class EspBleScanner
{
public:
  using ResultCallback = std::function<void(const EspBleScanResult &result)>;

  void onResult(ResultCallback callback);
  bool start(const EspBleScanConfig &config = EspBleScanConfig());
  bool stop();
  bool isScanning() const;
  size_t droppedResultCount() const;
#ifdef ESP_BLE_BLUEDROID_TESTING
  bool injectResultForTest(const EspBleScanResult &result);
  size_t pendingResultCountForTest() const;
#endif

private:
  friend class EspBleBluedroid;
  friend struct EspBleScannerImpl;

  explicit EspBleScanner(EspBleBluedroid *owner);
  ~EspBleScanner();
  void dispatchPendingResults();
  void flushPendingResults();
  // Drop everything a finished lifecycle left behind: queued results, the drop
  // count, the duplicate-address set and the scanning flag. Without it an address
  // reported before end() is still in the duplicate set after the next begin(), so
  // the first scan of the new lifecycle silently omits that peer.
  void resetBackend();

  EspBleBluedroid *owner_;
  ResultCallback resultCallback_;
  EspBleScannerImpl *impl_ = nullptr;
};

class EspBleGattServer
{
public:
  static constexpr size_t MaxServices = 8;
  static constexpr size_t MaxCharacteristics = 32;
  static constexpr size_t MaxDescriptors = 16;
  using WriteCallback = std::function<void(const EspBleGattWrite &write)>;
  using ReadCallback =
    std::function<void(const EspBleGattReadRequest &request)>;
  using DescriptorWriteCallback =
    std::function<void(const EspBleGattDescriptorWrite &write)>;
  using SubscriptionCallback =
    std::function<void(const EspBleGattSubscription &subscription)>;
  using SendCallback =
    std::function<void(const EspBleGattSendResult &result)>;

  // Register a Service. Two calls with the same UUID create two independent
  // instances, as the spec allows. Returns an invalid handle on failure.
  EspBleGattService addService(const char *serviceUuid);
  // Register a Characteristic inside a Service and return its handle; every
  // later operation takes that handle. Two characteristics in one service may
  // share a UUID, as the spec allows (the several HID Report characteristics of
  // a keyboard are the everyday case): the attribute table is built here and
  // each call returns its own handle, so a shared UUID is never ambiguous. Two
  // services may share a UUID as well (see addService).
  EspBleGattCharacteristic addCharacteristic(
    EspBleGattService service,
    const char *characteristicUuid,
    const EspBleGattCharacteristicConfig &config);
  // Register a Descriptor under a Characteristic. Unlike services and
  // characteristics, one characteristic may not carry the same descriptor UUID
  // twice: a descriptor is looked up by UUID within its characteristic, so a
  // duplicate would be unreachable and is rejected with InvalidArgument.
  EspBleGattDescriptor addDescriptor(
    EspBleGattCharacteristic characteristic,
    const char *descriptorUuid,
    const EspBleGattDescriptorConfig &config = EspBleGattDescriptorConfig());
  bool setValue(
    EspBleGattCharacteristic characteristic, const uint8_t *data, size_t length);
  bool setValue(EspBleGattCharacteristic characteristic, const String &value);
  bool value(EspBleGattCharacteristic characteristic, String &value) const;
  bool setDescriptorValue(
    EspBleGattDescriptor descriptor, const uint8_t *data, size_t length);
  bool setDescriptorValue(EspBleGattDescriptor descriptor, const String &value);
  bool descriptorValue(EspBleGattDescriptor descriptor, String &value) const;
  bool notify(
    EspBleGattCharacteristic characteristic, const uint8_t *data, size_t length);
  bool notify(EspBleGattCharacteristic characteristic, const String &value);
  bool indicate(
    EspBleGattCharacteristic characteristic, const uint8_t *data, size_t length);
  bool indicate(EspBleGattCharacteristic characteristic, const String &value);
  bool notify(
    EspBleConnectionId connectionId, EspBleGattCharacteristic characteristic,
    const uint8_t *data, size_t length);
  bool notify(
    EspBleConnectionId connectionId, EspBleGattCharacteristic characteristic,
    const String &value);
  bool indicate(
    EspBleConnectionId connectionId, EspBleGattCharacteristic characteristic,
    const uint8_t *data, size_t length);
  bool indicate(
    EspBleConnectionId connectionId, EspBleGattCharacteristic characteristic,
    const String &value);
  void onWritten(WriteCallback callback);
  void onRead(ReadCallback callback);
  void onDescriptorWritten(DescriptorWriteCallback callback);
  void onSubscriptionChanged(SubscriptionCallback callback);
  void onSent(SendCallback callback);
  // Additional observers that coexist with the primary and with each other, so a
  // profile helper and application code can both watch the same event. Returns a
  // listener id (EspBleInvalidListenerId if the list is full or the callback is
  // empty); removeListener() drops one by id.
  EspBleListenerId addWrittenListener(WriteCallback callback);
  EspBleListenerId addDescriptorWrittenListener(DescriptorWriteCallback callback);
  EspBleListenerId addSubscriptionChangedListener(SubscriptionCallback callback);
  EspBleListenerId addSentListener(SendCallback callback);
  bool removeListener(EspBleListenerId listenerId);

private:
  friend class EspBleBluedroid;
  friend struct EspBleGattServerImpl;
  friend class EspBleHidKeyboard;
  friend class EspBleHidMouse;
  friend class EspBleHidConsumerControl;
  friend class EspBleHidSystemControl;
  friend class EspBleHidGamepad;
  friend class EspBleHidVendor;
  friend class EspBleHidCustom;
  friend struct EspBleHidDeviceManagerImpl;
  explicit EspBleGattServer(EspBleBluedroid *owner);
  // Raise an already-registered Characteristic's read/write permission tiers.
  // HOGP requires encryption on the HID attributes, and that is only known at
  // begin() — after the attributes were registered. Deliberately not public: a
  // sketch declares the tiers in the config it passes to addCharacteristic().
  bool setEncryptionRequirement(
    EspBleGattCharacteristic characteristic, bool encryptedRead,
    bool encryptedWrite);
  bool setDescriptorEncryptionRequirement(
    EspBleGattDescriptor descriptor, bool encryptedRead);
  ~EspBleGattServer();
  bool realize();
  void resetBackend();
  void update();
  bool send(
    EspBleConnectionId connectionId, EspBleGattCharacteristic characteristic,
    const uint8_t *data, size_t length, bool indication);

  EspBleBluedroid *owner_;
  EspBleGattServerImpl *impl_ = nullptr;
  mutable std::mutex listenerMutex_;
  EspBleListenerId nextListenerId_ = 1;
  EspBleListenerId allocateListenerIdLocked();
  EspBleCallbackList<WriteCallback> writtenListeners_;
  // onRead() has no listener form: it decides the value before the response goes
  // out, which is an answer rather than an observation.
  ReadCallback readCallback_;
  EspBleCallbackList<DescriptorWriteCallback> descriptorWrittenListeners_;
  EspBleCallbackList<SubscriptionCallback> subscriptionListeners_;
  EspBleCallbackList<SendCallback> sentListeners_;
};

class EspBluedroidClassicInquiry
{
public:
  using ResultCallback =
    std::function<void(const EspBluedroidClassicInquiryResult &result)>;
  using CompleteCallback =
    std::function<void(const EspBluedroidClassicInquiryComplete &event)>;

  void onResult(ResultCallback callback);
  void onComplete(CompleteCallback callback);
  bool start(
    const EspBluedroidClassicInquiryConfig &config =
      EspBluedroidClassicInquiryConfig());
  bool stop();
  bool isRunning() const;
  size_t droppedResultCount() const;

private:
  friend class EspBluedroidClassic;
  friend struct EspBluedroidClassicInquiryImpl;

  explicit EspBluedroidClassicInquiry(EspBleBluedroid *owner);
  ~EspBluedroidClassicInquiry();
  bool begin(const char *deviceName);
  void end();
  void update();

  EspBleBluedroid *owner_;
  ResultCallback resultCallback_;
  CompleteCallback completeCallback_;
  EspBluedroidClassicInquiryImpl *impl_ = nullptr;
};

class EspBluedroidSpp
{
public:
  static constexpr size_t WriteQueueCapacity = 8;
  static constexpr size_t ReceiveBufferCapacity = 2048;
  static constexpr size_t MaximumWriteSize = 990;
  using ServerStartedCallback = std::function<void()>;
  using SessionCallback =
    std::function<void(const EspBluedroidSppSession &session)>;
  using DataCallback = std::function<void(const EspBluedroidSppData &event)>;
  using WriteCompletedCallback =
    std::function<void(const EspBluedroidSppWriteResult &result)>;
  using ConnectionFailureCallback =
    std::function<void(const EspBluedroidSppConnectionFailure &failure)>;

  void onServerStarted(ServerStartedCallback callback);
  void onConnected(SessionCallback callback);
  void onDisconnected(SessionCallback callback);
  void onData(DataCallback callback);
  void onWriteCompleted(WriteCompletedCallback callback);
  void onConnectionFailed(ConnectionFailureCallback callback);
  bool connect(
    const char *address,
    uint32_t timeoutMilliseconds = 10000,
    EspBluedroidSppSecurity security = EspBluedroidSppSecurity::None);
  bool startServer(
    const EspBluedroidSppServerConfig &config =
      EspBluedroidSppServerConfig());
  bool stopServer();
  bool serverRunning() const;
  size_t sessionCount() const;
  bool session(
    EspBluedroidSppSessionId sessionId,
    EspBluedroidSppSession &session) const;
  bool write(
    EspBluedroidSppSessionId sessionId,
    const uint8_t *data,
    size_t length);
  bool write(
    EspBluedroidSppSessionId sessionId,
    const String &value);
  bool disconnect(EspBluedroidSppSessionId sessionId);
  size_t pendingWriteCount() const;
  size_t pendingWriteCount(EspBluedroidSppSessionId sessionId) const;
  size_t droppedWriteCount() const;
  size_t available(EspBluedroidSppSessionId sessionId) const;
  int peek(EspBluedroidSppSessionId sessionId) const;
  int read(EspBluedroidSppSessionId sessionId);
  size_t read(
    EspBluedroidSppSessionId sessionId,
    uint8_t *data,
    size_t length);
  size_t droppedReceiveByteCount() const;
  size_t droppedEventCount() const;

private:
  friend class EspBluedroidClassic;
  friend class EspBluedroidSppSerial;
  friend struct EspBluedroidSppImpl;

  explicit EspBluedroidSpp(EspBleBluedroid *owner);
  ~EspBluedroidSpp();
  bool begin();
  void end();
  void update();

  EspBleBluedroid *owner_;
  ServerStartedCallback serverStartedCallback_;
  SessionCallback connectedCallback_;
  SessionCallback disconnectedCallback_;
  DataCallback dataCallback_;
  WriteCompletedCallback writeCompletedCallback_;
  ConnectionFailureCallback connectionFailedCallback_;
  EspBluedroidSppImpl *impl_ = nullptr;
};

class EspBluedroidSppSerial : public Stream
{
public:
  explicit EspBluedroidSppSerial(EspBleBluedroid &bluetooth);

  bool connected() const;
  explicit operator bool() const;
  EspBluedroidSppSessionId sessionId() const;

  int available() override;
  int peek() override;
  int read() override;
  int availableForWrite() override;
  void flush() override;
  size_t write(uint8_t value) override;
  size_t write(const uint8_t *data, size_t length) override;
  using Print::write;

private:
  EspBluedroidSppSessionId resolvedSessionId() const;

  EspBluedroidSpp &spp_;
};

class EspBluedroidA2dpSink
{
public:
  using SessionCallback =
    std::function<void(const EspBluedroidA2dpSession &session)>;
  using StreamCallback =
    std::function<void(const EspBluedroidA2dpStreamChanged &event)>;
  using PcmDataCallback =
    std::function<void(const EspBluedroidA2dpPcmData &data)>;
  using StartCallback =
    std::function<void(const EspBluedroidA2dpStartResult &result)>;
  using ConnectionFailureCallback = std::function<void(
    const EspBluedroidA2dpConnectionFailure &failure)>;

  bool start();
  bool stop();
  bool started() const;
  bool connect(const char *peerAddress);
  bool disconnect(EspBluedroidA2dpSessionId sessionId);
  bool session(EspBluedroidA2dpSession &session) const;
  size_t droppedEventCount() const;
  void onConnected(SessionCallback callback);
  void onDisconnected(SessionCallback callback);
  void onStarted(StartCallback callback);
  void onConnectionFailed(ConnectionFailureCallback callback);
  void onStreamChanged(StreamCallback callback);
  // PCM is delivered synchronously from the A2DP stack task. The data pointer
  // is valid only for the duration of the callback; do not block or retain it.
  void onPcmData(PcmDataCallback callback);

private:
  friend class EspBluedroidClassic;
  friend struct EspBluedroidA2dpImpl;
  explicit EspBluedroidA2dpSink(EspBleBluedroid *owner);
  ~EspBluedroidA2dpSink();
  void end();
  void update();

  EspBleBluedroid *owner_;
  EspBluedroidA2dpImpl *impl_ = nullptr;
  SessionCallback connectedCallback_;
  SessionCallback disconnectedCallback_;
  StartCallback startedCallback_;
  ConnectionFailureCallback connectionFailureCallback_;
  StreamCallback streamCallback_;
  PcmDataCallback pcmDataCallback_;
};

class EspBluedroidA2dpSource
{
public:
  using SessionCallback =
    std::function<void(const EspBluedroidA2dpSession &session)>;
  using StreamCallback =
    std::function<void(const EspBluedroidA2dpStreamChanged &event)>;
  using StartCallback =
    std::function<void(const EspBluedroidA2dpStartResult &result)>;
  using ConnectionFailureCallback = std::function<void(
    const EspBluedroidA2dpConnectionFailure &failure)>;
  using PcmRequestCallback =
    std::function<void(EspBluedroidA2dpPcmRequest &request)>;

  bool start();
  bool stop();
  bool started() const;
  bool connect(const char *peerAddress);
  bool disconnect(EspBluedroidA2dpSessionId sessionId);
  bool session(EspBluedroidA2dpSession &session) const;
  size_t droppedEventCount() const;
  bool startStream();
  bool suspendStream();
  // Called synchronously from the A2DP stack task. Fill request.data, set
  // request.written <= request.capacity, and return quickly.
  void onPcmRequested(PcmRequestCallback callback);
  void onConnected(SessionCallback callback);
  void onDisconnected(SessionCallback callback);
  void onStarted(StartCallback callback);
  void onConnectionFailed(ConnectionFailureCallback callback);
  void onStreamChanged(StreamCallback callback);

private:
  friend class EspBluedroidClassic;
  friend struct EspBluedroidA2dpImpl;
  explicit EspBluedroidA2dpSource(EspBleBluedroid *owner);
  ~EspBluedroidA2dpSource();
  void end();
  void update();

  EspBleBluedroid *owner_;
  EspBluedroidA2dpImpl *impl_ = nullptr;
  SessionCallback connectedCallback_;
  SessionCallback disconnectedCallback_;
  StartCallback startedCallback_;
  ConnectionFailureCallback connectionFailureCallback_;
  StreamCallback streamCallback_;
  PcmRequestCallback pcmRequestCallback_;
};

class EspBluedroidHfpHandsFree
{
public:
  using SessionCallback =
    std::function<void(const EspBluedroidHfpSession &session)>;
  using AudioCallback =
    std::function<void(const EspBluedroidHfpAudioChanged &event)>;
  using PcmDataCallback =
    std::function<void(const EspBluedroidHfpPcmData &data)>;
  using PcmRequestCallback =
    std::function<void(EspBluedroidHfpPcmRequest &request)>;
  using StartCallback =
    std::function<void(const EspBluedroidHfpStartResult &result)>;
  using ConnectionFailureCallback = std::function<void(
    const EspBluedroidHfpConnectionFailure &failure)>;

  bool start();
  bool stop();
  bool started() const;
  bool connect(const char *peerAddress);
  bool disconnect(EspBluedroidHfpSessionId sessionId);
  bool connectAudio(EspBluedroidHfpSessionId sessionId);
  bool disconnectAudio(EspBluedroidHfpSessionId sessionId);
  bool session(EspBluedroidHfpSession &session) const;
  size_t droppedEventCount() const;
  void onStarted(StartCallback callback);
  void onConnected(SessionCallback callback);
  void onDisconnected(SessionCallback callback);
  void onConnectionFailed(ConnectionFailureCallback callback);
  void onAudioChanged(AudioCallback callback);
  // Both PCM callbacks run synchronously on the HFP stack task. Buffers are
  // valid only during the callback; return quickly and do not retain them.
  void onPcmData(PcmDataCallback callback);
  void onPcmRequested(PcmRequestCallback callback);

private:
  friend class EspBluedroidClassic;
  friend struct EspBluedroidHfpImpl;
  explicit EspBluedroidHfpHandsFree(EspBleBluedroid *owner);
  ~EspBluedroidHfpHandsFree();
  void end();
  void update();

  EspBleBluedroid *owner_;
  EspBluedroidHfpImpl *impl_ = nullptr;
  StartCallback startedCallback_;
  SessionCallback connectedCallback_;
  SessionCallback disconnectedCallback_;
  ConnectionFailureCallback connectionFailureCallback_;
  AudioCallback audioCallback_;
  PcmDataCallback pcmDataCallback_;
  PcmRequestCallback pcmRequestCallback_;
};

class EspBluedroidHfpAudioGateway
{
public:
  using SessionCallback =
    std::function<void(const EspBluedroidHfpSession &session)>;
  using AudioCallback =
    std::function<void(const EspBluedroidHfpAudioChanged &event)>;
  using PcmDataCallback =
    std::function<void(const EspBluedroidHfpPcmData &data)>;
  using PcmRequestCallback =
    std::function<void(EspBluedroidHfpPcmRequest &request)>;
  using StartCallback =
    std::function<void(const EspBluedroidHfpStartResult &result)>;
  using ConnectionFailureCallback = std::function<void(
    const EspBluedroidHfpConnectionFailure &failure)>;

  bool start();
  bool stop();
  bool started() const;
  bool connect(const char *peerAddress);
  bool disconnect(EspBluedroidHfpSessionId sessionId);
  bool connectAudio(EspBluedroidHfpSessionId sessionId);
  bool disconnectAudio(EspBluedroidHfpSessionId sessionId);
  bool session(EspBluedroidHfpSession &session) const;
  size_t droppedEventCount() const;
  void onStarted(StartCallback callback);
  void onConnected(SessionCallback callback);
  void onDisconnected(SessionCallback callback);
  void onConnectionFailed(ConnectionFailureCallback callback);
  void onAudioChanged(AudioCallback callback);
  void onPcmData(PcmDataCallback callback);
  void onPcmRequested(PcmRequestCallback callback);

private:
  friend class EspBluedroidClassic;
  friend struct EspBluedroidHfpImpl;
  explicit EspBluedroidHfpAudioGateway(EspBleBluedroid *owner);
  ~EspBluedroidHfpAudioGateway();
  void end();
  void update();

  EspBleBluedroid *owner_;
  EspBluedroidHfpImpl *impl_ = nullptr;
  StartCallback startedCallback_;
  SessionCallback connectedCallback_;
  SessionCallback disconnectedCallback_;
  ConnectionFailureCallback connectionFailureCallback_;
  AudioCallback audioCallback_;
  PcmDataCallback pcmDataCallback_;
  PcmRequestCallback pcmRequestCallback_;
};

class EspBluedroidAvrcpController
{
public:
  using ConnectionCallback =
    std::function<void(const EspBluedroidAvrcpConnection &connection)>;
  using CommandCallback =
    std::function<void(const EspBluedroidAvrcpCommandEvent &event)>;
  using VolumeCallback =
    std::function<void(const EspBluedroidAvrcpVolumeEvent &event)>;

  bool start();
  bool stop();
  bool started() const;
  bool connected() const;
  String peerAddress() const;
  bool sendCommand(
    EspBluedroidAvrcpCommand command,
    EspBluedroidAvrcpKeyState state);
  bool click(EspBluedroidAvrcpCommand command);
  bool setAbsoluteVolume(uint8_t volume);
  size_t droppedEventCount() const;
  void onConnected(ConnectionCallback callback);
  void onDisconnected(ConnectionCallback callback);
  void onCommandResponse(CommandCallback callback);
  void onAbsoluteVolumeChanged(VolumeCallback callback);

private:
  friend class EspBluedroidClassic;
  explicit EspBluedroidAvrcpController(EspBleBluedroid *owner);
  ~EspBluedroidAvrcpController();
  void end();
  void update();

  EspBleBluedroid *owner_;
  EspBluedroidAvrcpControllerImpl *impl_ = nullptr;
  ConnectionCallback connectedCallback_;
  ConnectionCallback disconnectedCallback_;
  CommandCallback commandCallback_;
  VolumeCallback volumeCallback_;
};

class EspBluedroidAvrcpTarget
{
public:
  using ConnectionCallback =
    std::function<void(const EspBluedroidAvrcpConnection &connection)>;
  using CommandCallback =
    std::function<void(const EspBluedroidAvrcpCommandEvent &event)>;
  using VolumeCallback =
    std::function<void(const EspBluedroidAvrcpVolumeEvent &event)>;

  bool start();
  bool stop();
  bool started() const;
  bool connected() const;
  String peerAddress() const;
  bool setAbsoluteVolume(uint8_t volume);
  uint8_t absoluteVolume() const;
  size_t droppedEventCount() const;
  void onConnected(ConnectionCallback callback);
  void onDisconnected(ConnectionCallback callback);
  void onCommand(CommandCallback callback);
  void onAbsoluteVolumeRequested(VolumeCallback callback);

private:
  friend class EspBluedroidClassic;
  explicit EspBluedroidAvrcpTarget(EspBleBluedroid *owner);
  ~EspBluedroidAvrcpTarget();
  void end();
  void update();

  EspBleBluedroid *owner_;
  EspBluedroidAvrcpTargetImpl *impl_ = nullptr;
  ConnectionCallback connectedCallback_;
  ConnectionCallback disconnectedCallback_;
  CommandCallback commandCallback_;
  VolumeCallback volumeCallback_;
};

class EspBluedroidClassic
{
public:
  using SecurityChangedCallback =
    std::function<void(const EspBluedroidClassicSecurityChanged &event)>;
  using NumericComparisonCallback =
    std::function<void(const EspBluedroidClassicNumericComparison &event)>;
  using PasskeyDisplayedCallback =
    std::function<void(const EspBluedroidClassicPasskeyDisplayed &event)>;
  using PasskeyRequestedCallback =
    std::function<void(const EspBluedroidClassicPasskeyRequested &event)>;

  EspBluedroidClassicInquiry &inquiry();
  EspBluedroidSpp &spp();
  EspBluedroidA2dpSink &a2dpSink();
  EspBluedroidA2dpSource &a2dpSource();
  EspBluedroidAvrcpController &avrcpController();
  EspBluedroidAvrcpTarget &avrcpTarget();
  EspBluedroidHfpHandsFree &hfpHandsFree();
  EspBluedroidHfpAudioGateway &hfpAudioGateway();
  EspBluedroidClassicProfileSupport profileSupport(
    EspBluedroidClassicProfile profile) const;
  void onSecurityChanged(SecurityChangedCallback callback);
  void onNumericComparisonRequested(NumericComparisonCallback callback);
  void onPasskeyDisplayed(PasskeyDisplayedCallback callback);
  void onPasskeyRequested(PasskeyRequestedCallback callback);
  bool confirmNumericComparison(const char *peerAddress, bool accept);
  bool providePasskey(const char *peerAddress, uint32_t passkey);
  size_t bondCount() const;
  bool bond(size_t index, EspBluedroidClassicBond &bond) const;
  bool deleteBond(const EspBluedroidClassicBond &bond);
  bool deleteAllBonds();

private:
  friend class EspBleBluedroid;
  friend struct EspBluedroidClassicImpl;

  explicit EspBluedroidClassic(EspBleBluedroid *owner);
  ~EspBluedroidClassic();
  bool begin(
    const char *deviceName,
    const EspBluedroidClassicSecurityConfig &security);
  void end();
  void update();

  EspBleBluedroid *owner_;
  EspBluedroidClassicInquiry inquiry_;
  EspBluedroidSpp spp_;
  EspBluedroidA2dpSink a2dpSink_;
  EspBluedroidA2dpSource a2dpSource_;
  EspBluedroidAvrcpController avrcpController_;
  EspBluedroidAvrcpTarget avrcpTarget_;
  EspBluedroidHfpHandsFree hfpHandsFree_;
  EspBluedroidHfpAudioGateway hfpAudioGateway_;
  SecurityChangedCallback securityChangedCallback_;
  NumericComparisonCallback numericComparisonCallback_;
  PasskeyDisplayedCallback passkeyDisplayedCallback_;
  PasskeyRequestedCallback passkeyRequestedCallback_;
  EspBluedroidClassicImpl *impl_ = nullptr;
};

// HID over GATT keyboard device. `configure()` registers the HID service and its
// attributes, so it must be called before `begin()`, and reports go out to a host
// that has connected, paired (when security is enabled) and subscribed — which is
// what `ready()` reports.
class EspBleHidKeyboard
{
public:
  using OutputReportCallback =
    std::function<void(const EspBleHidKeyboardOutputReport &report)>;
  using ProtocolModeCallback =
    std::function<void(uint8_t mode, EspBleConnectionId connectionId)>;

  // HID over GATT Protocol Mode values (Protocol Mode characteristic 0x2A4E).
  static constexpr uint8_t BootProtocolMode = 0;
  static constexpr uint8_t ReportProtocolMode = 1;

  bool configure(
    const EspBleHidKeyboardConfig &config = EspBleHidKeyboardConfig());
  void enableNkro(bool enable = true);
  bool nkroEnabled() const;
  bool sendReport(const EspBleHidKeyboardReport &report);
  // Send the whole NKRO state as one notification, which the 6-key overload
  // cannot do: it carries keys[6] and is expanded into the bitmap, so only six
  // usages fit per report even with NKRO enabled. Requires enableNkro() before
  // configure(); fails with InvalidState otherwise.
  bool sendReport(const EspBleHidKeyboardNkroReport &report);
  // The NKRO state the host was last told about, for callers that build the whole
  // state each cycle. NKRO only: with NKRO disabled this stays cleared, because a
  // 6KRO report is held as the 8-byte wire value instead.
  const EspBleHidKeyboardNkroReport &heldState() const;
  // A subscribed HID Host is present and reports can go out right now: a
  // Peripheral connection that is encrypted (when security is enabled) and has
  // subscribed to this profile's Input Report CCCD (the Boot Keyboard Input CCCD
  // while the Host selected Boot Protocol Mode). sendReport() on a false ready()
  // fails with InvalidState, so poll this instead of inferring connectivity from
  // the send result.
  bool ready() const;
  bool pressUsage(uint8_t usage, uint8_t modifiers = 0, uint32_t holdMs = 10);
  bool releaseUsage(uint8_t usage);
  bool tapUsage(uint8_t usage, uint8_t modifiers = 0, uint32_t holdMs = 10);
  bool pressKey(char key, uint32_t holdMs = 10);
  bool tapKey(char key, uint32_t holdMs = 10);
  bool write(const char *text, uint32_t interKeyDelayMs = 5);
  bool releaseAll();
  void setLayout(EspBleKeyboardLayout layout);
  EspBleKeyboardLayout layout() const;
  bool setBatteryLevel(uint8_t level);
  void onOutputReport(OutputReportCallback callback);
  // The LED state (Caps Lock and friends) a host last wrote, for callers that
  // need to answer "what is it now?" rather than react to onOutputReport().
  // Both protocol modes are covered. Cleared before any host has written, when
  // the last host disconnects, and on re-initialisation, so a previous host's
  // LEDs are never reported as the current one's.
  EspBleHidKeyboardOutputReport ledState() const;
  // Current HID Protocol Mode (BootProtocolMode / ReportProtocolMode). The Host
  // selects it by writing the Protocol Mode characteristic; the default after a
  // connection is ReportProtocolMode.
  uint8_t protocolMode() const;
  void onProtocolMode(ProtocolModeCallback callback);
  bool configured() const;

private:
  friend class EspBleBluedroid;
  friend class EspBleHidMouse;
  friend class EspBleHidConsumerControl;
  friend class EspBleHidSystemControl;
  friend class EspBleHidGamepad;
  friend class EspBleHidVendor;
  friend class EspBleHidCustom;
  friend struct EspBleHidDeviceManagerImpl;

  explicit EspBleHidKeyboard(EspBleBluedroid *owner);
  // Register one more profile's Input Report in the shared HID service. The
  // keyboard owns the manager because it is the profile that carries the output
  // report and the protocol mode, so every other profile configures through here.
  bool configureProfile(uint8_t reportId, const EspBleHidDeviceConfig &config);
  // Bring up the attributes every HID device has whatever profiles it carries.
  // Called by whichever profile is configured first — including hidCustom(),
  // which adds no fixed profile at all — so the shared part exists once.
  bool configureCommon(const EspBleHidDeviceConfig &config);
  // The vendor profile is the only fixed one with an Output and a Feature report
  // of its own, so it registers two more 0x2A4D characteristics on top of the
  // Input Report configureProfile() added.
  bool configureVendorReports();
  // hidCustom() brings up the HID service without adding any fixed profile: its
  // reports are whatever the caller declared, so there is no descriptor to
  // compose and no report ID to reserve.
  bool configureCustom(const EspBleHidDeviceConfig &config);
  // Register one caller-declared report as its own 0x2A4D characteristic. Input
  // reports become notifiable, Output reports writable with or without a
  // response, Feature reports writable with a response only.
  bool registerCustomReport(size_t slot);
  int customInputSlot(uint8_t reportId) const;
  bool readyForCustom(uint8_t reportId) const;
  bool sendCustomInput(uint8_t reportId, const uint8_t *data, size_t length);
  ~EspBleHidKeyboard();
  // Called from begin(), when whether security is enabled is finally known: HOGP
  // requires encryption on the HID attributes, and the insufficient-encryption
  // error is what makes a host OS start pairing.
  bool applySecurity(bool securityEnabled);
  void resetBackend();
  bool sendRawReport(uint8_t reportId, const uint8_t *data, size_t length);
  // Put the held NKRO state on the wire as the 29-byte NKRO Input Report. Every
  // NKRO send path funnels through here so the layout is written down once.
  bool sendHeldNkroState();
  // The Host selected Boot Protocol Mode, so this report travels over the
  // dedicated Boot Keyboard Input Report instead of the Report-protocol one.
  bool useBootKeyboard(uint8_t reportId) const;
  bool readyFor(uint8_t reportId) const;

  EspBleBluedroid *owner_;
  EspBleHidDeviceManagerImpl *impl_ = nullptr;
  OutputReportCallback outputReportCallback_;
  ProtocolModeCallback protocolModeCallback_;
  EspBleKeyboardLayout layout_ = EspBleKeyboardLayout::EnUs;
  bool nkroEnabled_ = false;
  // The NKRO state the host was last told about. Holding it as the report type
  // itself keeps one definition of the modifier routing (0xE0-0xE7) and of the
  // bitmap layout, instead of repeating the bit math in every send path.
  EspBleHidKeyboardNkroReport nkroState_;
};


// The other HID device profiles. Each one joins the same HID service through the
// shared manager: one Report Map holds every profile's descriptor and the Report
// ID tells the reports apart, which is why `configure()` on any of them has to
// happen before `begin()`.
class EspBleHidMouse
{
public:
  bool configure(const EspBleHidMouseConfig &config = EspBleHidMouseConfig());
  bool configured() const;
  bool sendReport(const EspBleHidMouseReport &report);
  // See EspBleHidKeyboard::ready(): a subscribed HID Host can receive this
  // profile's reports right now.
  bool ready() const;
  bool move(int8_t x, int8_t y, int8_t wheel = 0, uint8_t buttons = 0);
  bool wheel(int8_t amount);
  bool press(uint8_t buttons);
  bool release(uint8_t buttons);
  bool click(uint8_t button, uint32_t holdMs = 10);
  bool releaseAll();
  uint8_t buttons() const;

private:
  friend class EspBleBluedroid;
  explicit EspBleHidMouse(EspBleBluedroid *owner) : owner_(owner) {}
  EspBleBluedroid *owner_;
  bool configured_ = false;
  uint8_t buttons_ = 0;
};

class EspBleHidConsumerControl
{
public:
  bool configure(
    const EspBleHidConsumerControlConfig &config = EspBleHidConsumerControlConfig());
  bool configured() const;
  bool sendReport(uint16_t usage);
  bool ready() const;
  bool sendUsage(uint16_t usage);
  bool press(uint16_t usage);
  bool release();
  bool click(uint16_t usage, uint32_t holdMs = 10);
  bool releaseAll();
  uint16_t usage() const;

private:
  friend class EspBleBluedroid;
  explicit EspBleHidConsumerControl(EspBleBluedroid *owner) : owner_(owner) {}
  EspBleBluedroid *owner_;
  bool configured_ = false;
  uint16_t usage_ = 0;
};

class EspBleHidSystemControl
{
public:
  bool configure(
    const EspBleHidSystemControlConfig &config = EspBleHidSystemControlConfig());
  bool configured() const;
  bool sendReport(uint8_t usage);
  bool ready() const;
  bool sendUsage(uint8_t usage);
  bool press(uint8_t usage);
  bool release();
  bool click(uint8_t usage, uint32_t holdMs = 10);
  bool releaseAll();
  uint8_t usage() const;

private:
  friend class EspBleBluedroid;
  explicit EspBleHidSystemControl(EspBleBluedroid *owner) : owner_(owner) {}
  EspBleBluedroid *owner_;
  bool configured_ = false;
  uint8_t usage_ = 0;
};

class EspBleHidGamepad
{
public:
  bool configure(const EspBleHidGamepadConfig &config = EspBleHidGamepadConfig());
  bool configured() const;
  bool sendReport(const EspBleHidGamepadReport &report);
  bool ready() const;
  bool send(int8_t x, int8_t y, int8_t z, int8_t rz, int8_t rx, int8_t ry,
            uint8_t hat, uint32_t buttons);
  bool releaseAll();

private:
  friend class EspBleBluedroid;
  explicit EspBleHidGamepad(EspBleBluedroid *owner) : owner_(owner) {}
  EspBleBluedroid *owner_;
  bool configured_ = false;
};

// A vendor-defined profile: one Input, one Output and one Feature report of a
// caller-chosen size, with bytes the library does not interpret. Unlike the
// profiles above it is bidirectional, so a host can write to the device.
class EspBleHidVendor
{
public:
  using ReportCallback = std::function<void(const EspBleHidVendorReport &report)>;

  bool configure(const EspBleHidVendorConfig &config = EspBleHidVendorConfig());
  bool configured() const;
  bool sendInput(const void *data, size_t length);
  // See EspBleHidKeyboard::ready(): a subscribed HID Host can receive this
  // profile's reports right now.
  bool ready() const;
  void onOutputReport(ReportCallback callback);
  void onFeatureReport(ReportCallback callback);

private:
  friend class EspBleBluedroid;
  friend class EspBleHidKeyboard;
  friend struct EspBleHidDeviceManagerImpl;
  explicit EspBleHidVendor(EspBleBluedroid *owner) : owner_(owner) {}

  EspBleBluedroid *owner_;
  bool configured_ = false;
  ReportCallback outputCallback_;
  ReportCallback featureCallback_;
};

// Custom HID with an arbitrary Report Descriptor. Reports are composed into the
// same HID service as the fixed profiles (keyboard/mouse/...), so a custom
// report can coexist with them. Report IDs must be unique and, when a fixed
// profile is also enabled, must not use its reserved report ID (1..6).
class EspBleHidCustom
{
public:
  static constexpr size_t MaxReports = 4;
  using ReportCallback = std::function<void(const EspBleHidVendorReport &report)>;

  bool configure(const EspBleHidDeviceConfig &config = EspBleHidDeviceConfig());
  // Set the raw HID Report Descriptor bytes exposed as the Report Map (0x2A4B).
  bool setReportMap(const uint8_t *descriptor, size_t length);
  bool addInputReport(uint8_t reportId, uint16_t sizeBytes);
  bool addOutputReport(uint8_t reportId, uint16_t sizeBytes);
  bool addFeatureReport(uint8_t reportId, uint16_t sizeBytes);
  bool configured() const;
  bool sendInput(uint8_t reportId, const uint8_t *data, size_t length);
  // See EspBleHidKeyboard::ready(), per declared Input Report: a subscribed HID
  // Host can receive this report right now. False for an unknown report ID.
  bool ready(uint8_t reportId) const;
  void onOutputReport(ReportCallback callback);
  void onFeatureReport(ReportCallback callback);

private:
  friend class EspBleBluedroid;
  friend class EspBleHidKeyboard;
  friend struct EspBleHidDeviceManagerImpl;
  explicit EspBleHidCustom(EspBleBluedroid *owner) : owner_(owner) {}
  bool addReport(uint8_t reportId, uint8_t reportType, uint16_t sizeBytes);

  EspBleBluedroid *owner_;
  bool configured_ = false;
  ReportCallback outputCallback_;
  ReportCallback featureCallback_;
};

// HID Host: the other side of HOGP. It discovers a peer device's HID service,
// reads the Report Map, pairs each Report characteristic with the report its
// Report Reference declares, subscribes to every Input Report, and turns the
// notifications into per-profile events using the descriptor the device published
// rather than an assumed layout.
//
// The report map parser is shared with the device side (`EspBleHidReportMap.h`,
// pinned by `tests/unit/report_map`), so what a host decodes is what a device
// declares.
class EspBleHidHost
{
public:
  static constexpr size_t MaxListenersPerEvent = 4;
  using DiscoveryCallback =
    std::function<void(const EspBleHidKeyboardHostDiscovery &result)>;
  using StateCallback = std::function<void(const EspBleHidKeyboardState &state)>;
  using KeyboardCallback = std::function<void(const EspBleHidKeyboardEvent &event)>;
  using MouseCallback = std::function<void(const EspBleHidMouseEvent &event)>;
  using ConsumerControlCallback =
    std::function<void(const EspBleHidConsumerControlEvent &event)>;
  using SystemControlCallback =
    std::function<void(const EspBleHidSystemControlEvent &event)>;
  using GamepadCallback = std::function<void(const EspBleHidGamepadEvent &event)>;
  using VendorInputCallback =
    std::function<void(const EspBleHidVendorInputEvent &event)>;

  // Start discovery of the peer's HID service on an established connection. The
  // result arrives through onDiscovered(); the many GATT operations it needs are
  // issued one at a time, because this backend allows one central GATT operation
  // per link (see examples/DIFFERENCES_FROM_ESPBLE.md).
  bool discover(EspBleConnectionId connectionId);
  bool setKeyboardLeds(
    EspBleConnectionId connectionId,
    bool numLock,
    bool capsLock,
    bool scrollLock,
    bool compose = false,
    bool kana = false);
  bool sendVendorOutput(
    EspBleConnectionId connectionId, const uint8_t *data, size_t length);
  bool sendVendorFeature(
    EspBleConnectionId connectionId, const uint8_t *data, size_t length);
  void onDiscovered(DiscoveryCallback callback);
  void onKeyboardState(StateCallback callback);
  void onKeyboard(KeyboardCallback callback);
  void onMouse(MouseCallback callback);
  void onConsumerControl(ConsumerControlCallback callback);
  void onSystemControl(SystemControlCallback callback);
  void onGamepad(GamepadCallback callback);
  void onVendorInput(VendorInputCallback callback);
  EspBleListenerId addDiscoveredListener(DiscoveryCallback callback);
  EspBleListenerId addKeyboardStateListener(StateCallback callback);
  EspBleListenerId addKeyboardListener(KeyboardCallback callback);
  EspBleListenerId addMouseListener(MouseCallback callback);
  EspBleListenerId addConsumerControlListener(ConsumerControlCallback callback);
  EspBleListenerId addSystemControlListener(SystemControlCallback callback);
  EspBleListenerId addGamepadListener(GamepadCallback callback);
  EspBleListenerId addVendorInputListener(VendorInputCallback callback);
  bool removeListener(EspBleListenerId listenerId);
  void setKeyboardLayout(EspBleKeyboardLayout layout);
  EspBleKeyboardLayout keyboardLayout() const;
  // Discovery finished and this link's Input Reports are subscribed, so reports
  // can arrive right now.
  bool ready(EspBleConnectionId connectionId) const;
  size_t droppedEventCount() const;
  size_t invalidInputReportCount() const;
  // Opt-in: after a HID peer that was discovered once reconnects and re-encrypts,
  // re-run discover() automatically (the HID Host does not use the generic
  // subscription registry, so it is not covered by persistentSubscriptions). Off
  // by default. Composes with a manual discover(): if the app still calls
  // discover() from onSecurityChanged, the automatic one is skipped for that
  // connection (no double discovery).
  void setAutoRediscover(bool enable);
  bool autoRediscover() const;

private:
  friend class EspBleBluedroid;
  friend struct EspBleHidKeyboardHostImpl;

  static constexpr size_t MaxRediscoverPeers = 4;

  explicit EspBleHidHost(EspBleBluedroid *owner);
  ~EspBleHidHost();
  void resetBackend();
  void handleDisconnected(EspBleConnectionId connectionId);
  void handleSecurityEstablished(const EspBleSecurityChanged &event);
  // Remember a peer this host discovered, so setAutoRediscover() only re-runs
  // discovery for a device it has seen before.
  void rememberRediscoverPeer(const String &address);
  EspBleHidKeyboardHostImpl *ensureImpl();

  EspBleBluedroid *owner_;
  EspBleHidKeyboardHostImpl *impl_ = nullptr;
  EspBleKeyboardLayout layout_ = EspBleKeyboardLayout::EnUs;
  bool autoRediscover_ = false;
};

class EspBleBluedroid
{
public:
  static constexpr size_t MaxDiscoveredGattServices = 16;
  static constexpr size_t MaxDiscoveredGattCharacteristics = 48;
  static constexpr size_t MaxDiscoveredGattDescriptors = 48;
  using ConnectionCallback =
    std::function<void(const EspBleConnection &connection)>;
  using ConnectionFailureCallback =
    std::function<void(const EspBleConnectionFailure &failure)>;
  using GattResultCallback = std::function<void(const EspBleGattResult &result)>;
  using SecurityChangedCallback =
    std::function<void(const EspBleSecurityChanged &event)>;
  using MtuChangedCallback =
    std::function<void(const EspBleMtuChanged &event)>;
  using PasskeyDisplayedCallback =
    std::function<void(const EspBlePasskeyDisplayed &event)>;
  using NotificationCallback =
    std::function<void(const EspBleGattNotification &notification)>;
  // The Client Characteristic Configuration Descriptor UUID, the descriptor a
  // Central writes to turn Notification or Indication on. Useful when walking
  // discoveredDescriptor() to find what a characteristic can be subscribed to.
  static constexpr const char *ClientCharacteristicConfigurationUuid =
    "00002902-0000-1000-8000-00805f9b34fb";
  // The HCI "remote user terminated connection" reason code, which is what a
  // peer reports in EspBleConnection::disconnectReason when this device closes
  // the link. Bluedroid does not let disconnect() choose the code it sends, so
  // this constant is for comparing a received reason (see
  // examples/DIFFERENCES_FROM_ESPBLE.md).
  static constexpr uint8_t DisconnectReasonRemoteUserTerminated = 0x13;

  EspBleBluedroid();
  ~EspBleBluedroid();

  EspBleBluedroid(const EspBleBluedroid &) = delete;
  EspBleBluedroid &operator=(const EspBleBluedroid &) = delete;

  bool begin(const EspBleConfig &config = EspBleConfig());
  void end();
  void update();

  bool initialized() const;
  // The current Public or Random Static address. Returns an empty String before
  // begin() and for controller-managed RPA, whose on-air value is not exposed
  // by the original ESP32 GAP API.
  String localAddress() const;
  EspBleAddressType localAddressType() const;
  // Set the nearest supported BLE radio level (-12..+9 dBm in 3 dB steps on
  // original ESP32). The applied advertising level is returned by txPower().
  bool setTxPower(int8_t dBm);
  int8_t txPower() const;
  EspBluedroidCapabilities capabilities() const;
  EspBleAdvertising &advertising();
  EspBleScanner &scanner();
  EspBleGattServer &gattServer();
  EspBleHidKeyboard &hidKeyboard();
  EspBleHidMouse &hidMouse();
  EspBleHidConsumerControl &hidConsumerControl();
  EspBleHidSystemControl &hidSystemControl();
  EspBleHidGamepad &hidGamepad();
  EspBleHidVendor &hidVendor();
  EspBleHidCustom &hidCustom();
  EspBleHidHost &hidHost();
  EspBluedroidClassic &classic();
#ifdef ESP_BLE_BLUEDROID_TESTING
  bool setSecurityResponseTimeoutForTest(uint32_t timeoutMilliseconds);
  bool injectNotificationForTest(
    const EspBleGattNotification &notification);
  bool injectGattResultForTest(const EspBleGattResult &result);
#endif

  bool connect(
    const EspBleScanResult &scanResult,
    uint32_t timeoutMilliseconds = 10000);
  bool connect(
    const char *address,
    EspBleAddressType addressType,
    uint32_t timeoutMilliseconds = 10000);
  bool disconnect(EspBleConnectionId connectionId);
  // Request new parameters for an active Central connection. Interval values
  // use 1.25 ms units and supervisionTimeout uses 10 ms units. A true return
  // value means that the backend accepted the request; observe the negotiated
  // result through onConnectionParametersUpdated().
  bool updateConnectionParameters(
    EspBleConnectionId connectionId,
    uint16_t minInterval,
    uint16_t maxInterval,
    uint16_t latency,
    uint16_t supervisionTimeout);
  bool discoverCharacteristic(
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    uint32_t timeoutMilliseconds = 10000);
  bool discoverServices(
    EspBleConnectionId connectionId,
    uint32_t timeoutMilliseconds = 10000);
  size_t discoveredServiceCount(EspBleConnectionId connectionId) const;
  bool discoveredService(
    EspBleConnectionId connectionId,
    size_t index,
    EspBleGattServiceInfo &service) const;
  size_t discoveredCharacteristicCount(
    EspBleConnectionId connectionId,
    const char *serviceUuid = nullptr) const;
  bool discoveredCharacteristic(
    EspBleConnectionId connectionId,
    size_t index,
    EspBleGattCharacteristicInfo &characteristic,
    const char *serviceUuid = nullptr) const;
  size_t discoveredDescriptorCount(
    EspBleConnectionId connectionId,
    const char *serviceUuid = nullptr,
    const char *characteristicUuid = nullptr) const;
  bool discoveredDescriptor(
    EspBleConnectionId connectionId,
    size_t index,
    EspBleGattDescriptorInfo &descriptor,
    const char *serviceUuid = nullptr,
    const char *characteristicUuid = nullptr) const;
  bool readCharacteristic(
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    uint32_t timeoutMilliseconds = 10000);
  bool writeCharacteristic(
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    const uint8_t *data,
    size_t length,
    bool response = true,
    uint32_t timeoutMilliseconds = 10000);
  bool readDescriptor(
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    const char *descriptorUuid,
    uint32_t timeoutMilliseconds = 10000);
  bool writeDescriptor(
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    const char *descriptorUuid,
    const uint8_t *data,
    size_t length,
    bool response = true,
    uint32_t timeoutMilliseconds = 10000);
  // Handle overloads select one exact characteristic from the discovery
  // snapshot, including characteristics that share a UUID.
  bool readCharacteristic(
    EspBleConnectionId connectionId,
    uint16_t characteristicHandle,
    uint32_t timeoutMilliseconds = 10000);
  bool writeCharacteristic(
    EspBleConnectionId connectionId,
    uint16_t characteristicHandle,
    const uint8_t *data,
    size_t length,
    bool response = true,
    uint32_t timeoutMilliseconds = 10000);
  bool writeCharacteristic(
    EspBleConnectionId connectionId,
    uint16_t characteristicHandle,
    const String &value,
    bool response = true,
    uint32_t timeoutMilliseconds = 10000);
  bool subscribe(
    EspBleConnectionId connectionId,
    uint16_t characteristicHandle,
    bool notifications = true,
    uint32_t timeoutMilliseconds = 10000);
  bool unsubscribe(
    EspBleConnectionId connectionId,
    uint16_t characteristicHandle,
    uint32_t timeoutMilliseconds = 10000);
  bool readDescriptor(
    EspBleConnectionId connectionId,
    uint16_t descriptorHandle,
    uint32_t timeoutMilliseconds = 10000);
  bool writeDescriptor(
    EspBleConnectionId connectionId,
    uint16_t descriptorHandle,
    const uint8_t *data,
    size_t length,
    bool response = true,
    uint32_t timeoutMilliseconds = 10000);
  bool writeDescriptor(
    EspBleConnectionId connectionId,
    uint16_t descriptorHandle,
    const String &value,
    bool response = true,
    uint32_t timeoutMilliseconds = 10000);
  bool subscribe(
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    bool notifications = true,
    uint32_t timeoutMilliseconds = 10000);
  bool unsubscribe(
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    uint32_t timeoutMilliseconds = 10000);
  bool writeCharacteristic(
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    const String &value,
    bool response = true,
    uint32_t timeoutMilliseconds = 10000);
  bool writeDescriptor(
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    const char *descriptorUuid,
    const String &value,
    bool response = true,
    uint32_t timeoutMilliseconds = 10000);
  // Every live link, whichever role this device holds on it. A GATT Server
  // connection (localRole Peripheral) is reported here just like a link this
  // device opened with connect(), so an application that logs or checks the MTU,
  // the encryption state or the peer address does not need to know which side
  // started the connection. One link per role is exposed.
  size_t connectionCount() const;
  bool connection(
    EspBleConnectionId connectionId, EspBleConnection &connection) const;
  bool requestSecurity(EspBleConnectionId connectionId);
  bool providePasskey(uint32_t passkey);
  bool confirmNumericComparison(bool accept);
  static constexpr size_t MaxAcceptListEntries = 8;
  bool addToAcceptList(
    const char *address, EspBleAddressType addressType);
  bool removeFromAcceptList(
    const char *address, EspBleAddressType addressType);
  void clearAcceptList();
  size_t acceptListCount() const;
  bool acceptListEntry(size_t index, EspBleBond &entry) const;
  size_t bondCount() const;
  bool bond(size_t index, EspBleBond &bond) const;
  bool deleteBond(const EspBleBond &bond);
  bool deleteAllBonds();
  size_t droppedEventCount() const;

  void onConnected(ConnectionCallback callback);
  void onDisconnected(ConnectionCallback callback);
  void onConnectionFailed(ConnectionFailureCallback callback);
  void onMtuChanged(MtuChangedCallback callback);
  void onConnectionParametersUpdated(ConnectionCallback callback);
  void onSecurityChanged(SecurityChangedCallback callback);
  void onPasskeyDisplayed(PasskeyDisplayedCallback callback);
  void onNumericComparison(PasskeyDisplayedCallback callback);
  // Additional connection-event observers that coexist with the primary on*()
  // callback and with each other, so a profile helper or an integration adapter
  // can watch connections without taking the application's slot. The primary
  // callback runs first, then the listeners in registration order. Remove any of
  // them with removeConnectionListener(); ids are unique across every listener
  // list on this object. onPasskeyDisplayed() / onNumericComparison() have no
  // listener form on purpose — they ask for an answer, not for an observer.
  EspBleListenerId addConnectedListener(ConnectionCallback callback);
  EspBleListenerId addDisconnectedListener(ConnectionCallback callback);
  EspBleListenerId addConnectionFailedListener(
    ConnectionFailureCallback callback);
  EspBleListenerId addMtuChangedListener(MtuChangedCallback callback);
  EspBleListenerId addConnectionParametersUpdatedListener(
    ConnectionCallback callback);
  EspBleListenerId addSecurityChangedListener(SecurityChangedCallback callback);
  bool removeConnectionListener(EspBleListenerId listenerId);
  void onCharacteristicDiscovered(GattResultCallback callback);
  void onCharacteristicRead(GattResultCallback callback);
  void onCharacteristicWritten(GattResultCallback callback);
  void onDescriptorRead(GattResultCallback callback);
  void onDescriptorWritten(GattResultCallback callback);
  void onSubscribed(GattResultCallback callback);
  void onUnsubscribed(GattResultCallback callback);
  void onNotification(NotificationCallback callback);
  // Additional GATT-client observers that coexist with the primary and each
  // other, so a profile helper and application code can both watch the same
  // event. Returns a listener id (EspBleInvalidListenerId if the list is full or
  // the callback is empty); removeGattListener() drops one.
  EspBleListenerId addCharacteristicDiscoveredListener(
    GattResultCallback callback);
  EspBleListenerId addCharacteristicReadListener(GattResultCallback callback);
  EspBleListenerId addCharacteristicWrittenListener(GattResultCallback callback);
  EspBleListenerId addServicesDiscoveredListener(GattResultCallback callback);
  EspBleListenerId addDescriptorReadListener(GattResultCallback callback);
  EspBleListenerId addDescriptorWrittenListener(GattResultCallback callback);
  EspBleListenerId addSubscribedListener(GattResultCallback callback);
  EspBleListenerId addUnsubscribedListener(GattResultCallback callback);
  EspBleListenerId addNotificationListener(NotificationCallback callback);
  bool removeGattListener(EspBleListenerId listenerId);
  void onServicesDiscovered(GattResultCallback callback);

  EspBleError lastError() const;
  const char *lastErrorName() const;
  const String &lastErrorDetail() const;
  void clearError();

private:
  friend class EspBleAdvertising;
  friend class EspBleScanner;
  friend class EspBleGattServer;
  friend class EspBleHidKeyboard;
  friend class EspBleHidMouse;
  friend class EspBleHidConsumerControl;
  friend class EspBleHidSystemControl;
  friend class EspBleHidGamepad;
  friend class EspBleHidVendor;
  friend class EspBleHidCustom;
  friend class EspBleHidHost;
  friend struct EspBleHidDeviceManagerImpl;
  friend struct EspBleHidKeyboardHostImpl;
  friend class EspBluedroidClassic;
  friend class EspBluedroidClassicInquiry;
  friend class EspBluedroidSpp;
  friend class EspBluedroidA2dpSink;
  friend class EspBluedroidA2dpSource;
  friend class EspBluedroidAvrcpController;
  friend class EspBluedroidAvrcpTarget;
  friend class EspBluedroidHfpHandsFree;
  friend class EspBluedroidHfpAudioGateway;
  friend struct EspBluedroidHfpImpl;
  friend struct EspBleGattServerImpl;

  // The peripheral half of the connection lifecycle, called from the GATT
  // Server's backend callbacks. They feed the same event queue and the same
  // snapshot as the central half, so onConnected() / onMtuChanged() /
  // onConnectionParametersUpdated() / onDisconnected() and connection() behave
  // the same in both roles. Defined here rather than in the server because the
  // connection state, the event queue and the security callbacks all live with
  // the owner.
  void peripheralConnected(
    uint16_t connectionHandle,
    const uint8_t *address,
    uint8_t addressType,
    uint16_t interval,
    uint16_t latency,
    uint16_t timeout);
  void peripheralMtuChanged(uint16_t connectionHandle, uint16_t mtu);
  void peripheralParametersUpdated(
    const uint8_t *address,
    uint16_t interval,
    uint16_t latency,
    uint16_t timeout);
  void peripheralDisconnected(uint16_t connectionHandle, int reason);
  // The runtime ID of the live peripheral link, or 0. The GATT Server reports it
  // on every write, read request, subscription and send result, so
  // connection(id) resolves the link a server event came from.
  EspBleConnectionId peripheralConnectionId() const;

  void setError(EspBleError error, const char *detail = nullptr);
  bool startGattOperation(
    EspBleGattOperation operation,
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    const uint8_t *data,
    size_t length,
    bool response,
    const char *descriptorUuid,
    uint32_t timeoutMilliseconds,
    uint16_t characteristicHandle = 0,
    uint16_t descriptorHandle = 0);
  void expireGattOperation();
  void dispatchConnectionEvents();
  // Copy the primary and the listeners out, then call them with the lock
  // released: a callback is free to add or remove listeners, and one that blocks
  // cannot stall a registration.
  template <typename Callback, typename Argument>
  void dispatchListeners(
    const EspBleCallbackList<Callback> &list, const Argument &argument) const
  {
    std::shared_ptr<Callback> callbacks[EspBleCallbackList<Callback>::Capacity];
    size_t count = 0;
    {
      std::lock_guard<std::mutex> lock(listenerMutex_);
      count = list.snapshot(callbacks);
    }
    for (size_t index = 0; index < count; ++index)
    {
      (*callbacks[index])(argument);
    }
  }

  EspBleAdvertising advertising_;
  EspBleScanner scanner_;
  EspBleGattServer gattServer_;
  EspBleHidKeyboard hidKeyboard_;
  EspBleHidMouse hidMouse_;
  EspBleHidConsumerControl hidConsumerControl_;
  EspBleHidSystemControl hidSystemControl_;
  EspBleHidGamepad hidGamepad_;
  EspBleHidVendor hidVendor_;
  EspBleHidCustom hidCustom_;
  EspBleHidHost hidHost_;
  EspBluedroidClassic classic_;
  EspBleConnectionImpl *connectionImpl_ = nullptr;
  // One list per event: the primary callback set by on*() plus the listeners.
  // The mutex serializes registration against dispatch; dispatch copies the
  // callbacks out and invokes them unlocked, so a callback may add or remove
  // listeners without deadlocking.
  mutable std::mutex listenerMutex_;
  EspBleListenerId nextListenerId_ = 1;
  EspBleListenerId allocateListenerIdLocked();
  EspBleCallbackList<ConnectionCallback> connectedListeners_;
  EspBleCallbackList<ConnectionCallback> disconnectedListeners_;
  EspBleCallbackList<ConnectionFailureCallback> connectionFailedListeners_;
  EspBleCallbackList<MtuChangedCallback> mtuChangedListeners_;
  EspBleCallbackList<ConnectionCallback> connectionParametersUpdatedListeners_;
  EspBleCallbackList<SecurityChangedCallback> securityChangedListeners_;
  PasskeyDisplayedCallback passkeyDisplayedCallback_;
  PasskeyDisplayedCallback numericComparisonCallback_;
  EspBleCallbackList<GattResultCallback> characteristicDiscoveredListeners_;
  EspBleCallbackList<GattResultCallback> characteristicReadListeners_;
  EspBleCallbackList<GattResultCallback> characteristicWrittenListeners_;
  EspBleCallbackList<GattResultCallback> descriptorReadListeners_;
  EspBleCallbackList<GattResultCallback> descriptorWrittenListeners_;
  EspBleCallbackList<GattResultCallback> subscribedListeners_;
  EspBleCallbackList<GattResultCallback> unsubscribedListeners_;
  EspBleCallbackList<GattResultCallback> servicesDiscoveredListeners_;
  EspBleCallbackList<NotificationCallback> notificationListeners_;
  bool initialized_ = false;
  String activeDeviceName_;
  uint16_t activePreferredMtu_ = 247;
  EspBleOwnAddressType activeOwnAddressType_ =
    EspBleOwnAddressType::Public;
  uint8_t activeRandomAddress_[6] = {};
  bool activeRandomAddressPresent_ = false;
  EspBleSecurityConfig activeSecurity_;
  EspBluedroidClassicSecurityConfig activeClassicSecurity_;
  EspBleBond acceptList_[MaxAcceptListEntries];
  size_t acceptListCount_ = 0;
  EspBleError lastError_ = EspBleError::None;
  String lastErrorDetail_;
};

#endif // ESP_BLE_BLUEDROID_H
