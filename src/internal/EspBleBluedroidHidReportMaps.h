#pragma once

// The HID Report Descriptors the HID over GATT device profiles publish, and the
// rules for composing them into the one Report Map characteristic (0x2A4B) a HID
// service carries.
//
// These bytes are the wire specification, not an implementation detail: a host OS
// parses them to learn what this device is, and a byte that differs from EspBle's
// makes the same sketch behave differently on the two libraries. They are
// therefore a straight port of EspBle's tables, pinned byte-for-byte by
// `tests/unit/hid_report_maps` against a snapshot taken from EspBle's source, and
// parsed back with the shared `EspBleHidReportMap.h` so the descriptors are also
// checked for meaning rather than only for equality.
//
// Kept out of the public header deliberately: EspBle exposes no equivalent, so a
// public symbol here would be a parity difference with nothing behind it. The
// profile classes that will publish these descriptors (`hidKeyboard()` and
// friends) are not implemented yet — the wire format lands first, on its own, the
// same order the BLE MIDI work followed.
//
// Report IDs are fixed per profile and shared with EspBle, because a host that
// learned report ID 2 is a mouse must find a mouse there:
//
//   1 keyboard   2 mouse   3 gamepad   4 consumer control   5 system control
//   6 vendor     7+ hidCustom() reports declare their own

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace espblebluedroid
{
namespace internal
{
namespace hid
{
// Profile bit positions inside the profile mask, in the order their descriptors
// are concatenated. The order is part of the wire format: a host reads the maps
// in this sequence.
enum class Profile : uint8_t
{
  Keyboard = 0,
  Mouse,
  Gamepad,
  ConsumerControl,
  SystemControl,
  Vendor,
};
static constexpr size_t ProfileCount = 6;

static constexpr uint8_t ReportIdKeyboard = 1;
static constexpr uint8_t ReportIdMouse = 2;
static constexpr uint8_t ReportIdGamepad = 3;
static constexpr uint8_t ReportIdConsumerControl = 4;
static constexpr uint8_t ReportIdSystemControl = 5;
static constexpr uint8_t ReportIdVendor = 6;

// Room for every profile at once plus a caller's own descriptor.
static constexpr size_t ReportMapCapacity = 640;
static constexpr size_t CustomReportMapCapacity = 256;

// 6KRO keyboard: 8 modifier bits, one reserved byte, six key usages, and the LED
// output byte. This is the descriptor almost every HOGP host expects.
static constexpr uint8_t KeyboardMap[] = {
  0x05, 0x01,       // Usage Page (Generic Desktop)
  0x09, 0x06,       // Usage (Keyboard)
  0xa1, 0x01,       // Collection (Application)
  0x85, 0x01,       // Report ID
  0x05, 0x07,       // Usage Page (Keyboard)
  0x19, 0xe0,       // Usage Minimum (Left Control)
  0x29, 0xe7,       // Usage Maximum (Right GUI)
  0x15, 0x00,       // Logical Minimum (0)
  0x25, 0x01,       // Logical Maximum (1)
  0x75, 0x01,       // Report Size (1)
  0x95, 0x08,       // Report Count (8)
  0x81, 0x02,       // Input (Data, Variable, Absolute)
  0x95, 0x01,       // Report Count (1)
  0x75, 0x08,       // Report Size (8)
  0x81, 0x01,       // Input (Constant)
  0x95, 0x06,       // Report Count (6)
  0x75, 0x08,       // Report Size (8)
  0x15, 0x00,       // Logical Minimum (0)
  0x25, 0x65,       // Logical Maximum (101)
  0x05, 0x07,       // Usage Page (Keyboard)
  0x19, 0x00,       // Usage Minimum (0)
  0x29, 0x65,       // Usage Maximum (101)
  0x81, 0x00,       // Input (Data, Array)
  0x95, 0x05,       // Report Count (5)
  0x75, 0x01,       // Report Size (1)
  0x05, 0x08,       // Usage Page (LEDs)
  0x19, 0x01,       // Usage Minimum (Num Lock)
  0x29, 0x05,       // Usage Maximum (Kana)
  0x91, 0x02,       // Output (Data, Variable, Absolute)
  0x95, 0x01,       // Report Count (1)
  0x75, 0x03,       // Report Size (3)
  0x91, 0x01,       // Output (Constant)
  0xc0              // End Collection
};

// N-key rollover: the six key slots are replaced by a 224-bit usage bitmap, so
// any number of keys can be held at once. The LED output block moves ahead of the
// bitmap, which is what keeps the output report at offset 0 of its own report.
static constexpr uint8_t NkroKeyboardMap[] = {
  0x05,0x01, 0x09,0x06, 0xa1,0x01, 0x85,0x01,
  0x05,0x07, 0x19,0xe0, 0x29,0xe7, 0x15,0x00, 0x25,0x01,
  0x75,0x01, 0x95,0x08, 0x81,0x02,
  0x05,0x08, 0x19,0x01, 0x29,0x05, 0x95,0x05, 0x75,0x01,
  0x91,0x02, 0x95,0x01, 0x75,0x03, 0x91,0x01,
  0x05,0x07, 0x19,0x00, 0x29,0xdf, 0x15,0x00, 0x25,0x01,
  0x75,0x01, 0x95,0xe0, 0x81,0x02, 0xc0
};

// Mouse: buttons, then X / Y / wheel as relative signed bytes. The button count
// is patched in at the offsets below.
static constexpr uint8_t MouseMap[] = {
  0x05,0x01, 0x09,0x02, 0xa1,0x01, 0x85,0x02, 0x09,0x01, 0xa1,0x00,
  0x05,0x09, 0x19,0x01, 0x29,0x05, 0x15,0x00, 0x25,0x01, 0x95,0x05,
  0x75,0x01, 0x81,0x02, 0x95,0x01, 0x75,0x03, 0x81,0x01, 0x05,0x01,
  0x09,0x30, 0x09,0x31, 0x09,0x38, 0x15,0x81, 0x25,0x7f, 0x75,0x08,
  0x95,0x03, 0x81,0x06, 0xc0, 0xc0};
// Usage Maximum of the button range, its Report Count, and the Report Size of the
// padding that follows: patched so the report stays eight bits wide whatever the
// button count.
static constexpr size_t MouseUsageMaximumOffset = 17;
static constexpr size_t MouseButtonCountOffset = 23;
static constexpr size_t MousePaddingSizeOffset = 31;

// Gamepad: six signed axes, an 8-direction hat switch, and 32 buttons.
static constexpr uint8_t GamepadMap[] = {
  0x05,0x01, 0x09,0x05, 0xa1,0x01, 0x85,0x03, 0x15,0x81, 0x25,0x7f,
  0x09,0x30, 0x09,0x31, 0x09,0x32, 0x09,0x35, 0x09,0x33, 0x09,0x34,
  0x75,0x08, 0x95,0x06, 0x81,0x02, 0x15,0x00, 0x25,0x08, 0x35,0x00,
  0x46,0x3b,0x01, 0x65,0x14, 0x09,0x39, 0x75,0x08, 0x95,0x01,
  0x81,0x02, 0x65,0x00, 0x05,0x09, 0x19,0x01, 0x29,0x20, 0x15,0x00,
  0x25,0x01, 0x75,0x01, 0x95,0x20, 0x81,0x02, 0xc0};

// Consumer control: one 16-bit usage per report, which is how volume and
// transport keys are sent.
static constexpr uint8_t ConsumerMap[] = {
  0x05,0x0c, 0x09,0x01, 0xa1,0x01, 0x85,0x04, 0x15,0x00, 0x26,0xff,0x03,
  0x19,0x00, 0x2a,0xff,0x03, 0x75,0x10, 0x95,0x01, 0x81,0x00, 0xc0};

// System control: power down, sleep and wake up as a single usage byte.
static constexpr uint8_t SystemMap[] = {
  0x05,0x01, 0x09,0x80, 0xa1,0x01, 0x85,0x05, 0x15,0x00, 0x25,0x03,
  0x19,0x00, 0x29,0x03, 0x75,0x08, 0x95,0x01, 0x81,0x00, 0xc0};

// Vendor-defined: input, output and feature reports of a caller-chosen size, on
// the vendor usage page. The three sizes are patched at the offsets below.
static constexpr uint8_t VendorMap[] = {
  0x06,0x00,0xff, 0x09,0x01, 0xa1,0x01, 0x85,0x06,
  0x15,0x00, 0x26,0xff,0x00, 0x75,0x08,
  0x09,0x01, 0x95,0x3f, 0x81,0x02,
  0x09,0x02, 0x95,0x3f, 0x91,0x02,
  0x09,0x03, 0x95,0x3f, 0xb1,0x02, 0xc0};
static constexpr size_t VendorInputSizeOffset = 19;
static constexpr size_t VendorOutputSizeOffset = 25;
static constexpr size_t VendorFeatureSizeOffset = 31;

// HID Information (0x2A4A): HID 1.11, no country code by default, remote-wake
// capable. Byte 2 is replaced by the configured country code.
static constexpr uint8_t HidInformation[4] = {0x11, 0x01, 0x00, 0x01};
static constexpr size_t HidInformationCountryOffset = 2;

// How much a mask of profiles needs, before any custom descriptor is appended.
inline size_t composedLength(uint8_t profileMask, bool nkroKeyboard)
{
  size_t length = 0;
  if ((profileMask & (1u << static_cast<uint8_t>(Profile::Keyboard))) != 0)
    length += nkroKeyboard ? sizeof(NkroKeyboardMap) : sizeof(KeyboardMap);
  if ((profileMask & (1u << static_cast<uint8_t>(Profile::Mouse))) != 0)
    length += sizeof(MouseMap);
  if ((profileMask & (1u << static_cast<uint8_t>(Profile::Gamepad))) != 0)
    length += sizeof(GamepadMap);
  if ((profileMask & (1u << static_cast<uint8_t>(Profile::ConsumerControl))) != 0)
    length += sizeof(ConsumerMap);
  if ((profileMask & (1u << static_cast<uint8_t>(Profile::SystemControl))) != 0)
    length += sizeof(SystemMap);
  if ((profileMask & (1u << static_cast<uint8_t>(Profile::Vendor))) != 0)
    length += sizeof(VendorMap);
  return length;
}

// Concatenate the descriptors of the selected profiles into `buffer`, in profile
// order, patching the configurable fields. Returns the number of bytes written,
// or 0 if they do not fit.
//
// One Report Map holds every profile of one device, which is why the profiles
// share a HID service instead of each publishing its own: a HID host expects one
// service and tells the reports apart by their Report ID.
inline size_t compose(
  uint8_t *buffer,
  size_t capacity,
  uint8_t profileMask,
  bool nkroKeyboard,
  uint8_t mouseButtonCount,
  uint8_t vendorReportSize)
{
  if (buffer == nullptr || composedLength(profileMask, nkroKeyboard) > capacity)
    return 0;
  size_t length = 0;
  const auto selected = [profileMask](Profile profile) {
    return (profileMask & (1u << static_cast<uint8_t>(profile))) != 0;
  };
  const auto append = [&](const uint8_t *data, size_t dataLength) {
    memcpy(buffer + length, data, dataLength);
    const size_t offset = length;
    length += dataLength;
    return offset;
  };

  if (selected(Profile::Keyboard))
  {
    if (nkroKeyboard) append(NkroKeyboardMap, sizeof(NkroKeyboardMap));
    else append(KeyboardMap, sizeof(KeyboardMap));
  }
  if (selected(Profile::Mouse))
  {
    const size_t offset = append(MouseMap, sizeof(MouseMap));
    buffer[offset + MouseUsageMaximumOffset] = mouseButtonCount;
    buffer[offset + MouseButtonCountOffset] = mouseButtonCount;
    buffer[offset + MousePaddingSizeOffset] =
      static_cast<uint8_t>(8 - mouseButtonCount);
  }
  if (selected(Profile::Gamepad)) append(GamepadMap, sizeof(GamepadMap));
  if (selected(Profile::ConsumerControl)) append(ConsumerMap, sizeof(ConsumerMap));
  if (selected(Profile::SystemControl)) append(SystemMap, sizeof(SystemMap));
  if (selected(Profile::Vendor))
  {
    const size_t offset = append(VendorMap, sizeof(VendorMap));
    buffer[offset + VendorInputSizeOffset] = vendorReportSize;
    buffer[offset + VendorOutputSizeOffset] = vendorReportSize;
    buffer[offset + VendorFeatureSizeOffset] = vendorReportSize;
  }
  return length;
}

// PnP ID (0x2A50) for the Device Information Service: vendor ID source 0x02
// (USB Implementers Forum) and the three configured identifiers, little-endian.
inline void composePnpId(
  uint8_t *buffer, uint16_t vendorId, uint16_t productId, uint16_t productVersion)
{
  if (buffer == nullptr) return;
  buffer[0] = 0x02;
  buffer[1] = static_cast<uint8_t>(vendorId);
  buffer[2] = static_cast<uint8_t>(vendorId >> 8);
  buffer[3] = static_cast<uint8_t>(productId);
  buffer[4] = static_cast<uint8_t>(productId >> 8);
  buffer[5] = static_cast<uint8_t>(productVersion);
  buffer[6] = static_cast<uint8_t>(productVersion >> 8);
}
static constexpr size_t PnpIdLength = 7;

}  // namespace hid
}  // namespace internal
}  // namespace espblebluedroid
