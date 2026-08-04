// Host-side unit tests for the HID Report Descriptors this library publishes
// (src/internal/EspBleBluedroidHidReportMaps.h) and for the way they are composed
// into one Report Map characteristic.
//
// Two different things are checked, and both are needed. The Python side of this
// suite compares the raw bytes against EspBle's tables, which catches drift
// between the two libraries. Here the composed map is fed back through the shared
// parser (EspBleHidReportMap.h, itself a verbatim copy of EspBle's) and what it
// recovers is asserted — so a descriptor equal to EspBle's but wrong is caught
// too, and a patched field (mouse button count, vendor report size) is checked for
// the meaning it is supposed to carry.
//
// Using the parser as the judge is deliberate: it is the same code the HID *host*
// role will run against a real keyboard, so the two halves of the profile are held
// to one description of the wire format.

#include "EspBleHidReportMap.h"
#include "internal/EspBleBluedroidHidReportMaps.h"

#include <cstdio>
#include <cstring>

namespace hid = espblebluedroid::internal::hid;

namespace
{
int failures = 0;

void check(const char *name, bool condition)
{
  if (!condition)
  {
    std::printf("FAIL %s\n", name);
    ++failures;
  }
}

constexpr uint8_t mask(hid::Profile profile)
{
  return static_cast<uint8_t>(1u << static_cast<uint8_t>(profile));
}

// The parsed entry for one report ID, or nullptr.
const EspBleHidReportMapEntry *entry(
  const EspBleHidReportMapInfo &info, uint8_t reportId)
{
  for (size_t index = 0; index < info.count; ++index)
  {
    if (info.entries[index].reportId == reportId) return &info.entries[index];
  }
  return nullptr;
}

void testEachProfileParsesBackToItself()
{
  struct Case
  {
    const char *name;
    hid::Profile profile;
    uint8_t reportId;
    EspBleHidReportKind kind;
    size_t inputBytes;
  };
  // The input report size a host has to allocate for each profile, read off the
  // descriptors: 1 modifier byte + 1 reserved + 6 keys; 1 button byte + X/Y/wheel;
  // 6 axes + hat + 4 button bytes; one 16-bit usage; one usage byte; the
  // configured vendor size.
  const Case cases[] = {
    {"keyboard", hid::Profile::Keyboard, hid::ReportIdKeyboard,
      EspBleHidReportKind::Keyboard, 8},
    {"mouse", hid::Profile::Mouse, hid::ReportIdMouse,
      EspBleHidReportKind::Mouse, 4},
    {"gamepad", hid::Profile::Gamepad, hid::ReportIdGamepad,
      EspBleHidReportKind::Gamepad, 11},
    {"consumer", hid::Profile::ConsumerControl, hid::ReportIdConsumerControl,
      EspBleHidReportKind::ConsumerControl, 2},
    {"system", hid::Profile::SystemControl, hid::ReportIdSystemControl,
      EspBleHidReportKind::SystemControl, 1},
    {"vendor", hid::Profile::Vendor, hid::ReportIdVendor,
      EspBleHidReportKind::Vendor, 63},
  };
  for (const Case &testCase : cases)
  {
    uint8_t buffer[hid::ReportMapCapacity];
    const size_t length =
      hid::compose(buffer, sizeof(buffer), mask(testCase.profile), false, 5, 63);
    check(testCase.name, length > 0);
    const EspBleHidReportMapInfo info = espBleParseHidReportMap(buffer, length);
    check(testCase.name, info.count == 1);
    const EspBleHidReportMapEntry *input = entry(info, testCase.reportId);
    check(testCase.name, input != nullptr);
    if (input == nullptr) continue;
    // The parser recognises the profile from the usage page and usage, so a
    // descriptor that carried the right size for the wrong device fails here.
    check(testCase.name, input->kind == testCase.kind);
    check(testCase.name, input->hasReportId);
    check(testCase.name, input->inputByteLength() == testCase.inputBytes);
  }
}

void testKeyboardCarriesTheLedOutputReport()
{
  uint8_t buffer[hid::ReportMapCapacity];
  const size_t length =
    hid::compose(buffer, sizeof(buffer), mask(hid::Profile::Keyboard), false, 5, 63);
  const EspBleHidKeyboardReportMapInfo keyboard =
    espBleParseKeyboardReportMap(buffer, length);
  check("keyboard recognised", keyboard.keyboardFound);
  check("keyboard has report id", keyboard.hasReportId);
  check("keyboard report id", keyboard.reportId == hid::ReportIdKeyboard);
  // The LED Output Report is how a host tells the device that Caps Lock is on. It
  // is the only writable part of the keyboard descriptor, so its absence would be
  // silent until a host tried.
  check("keyboard led output", keyboard.hasLedOutput);

  const EspBleHidReportMapInfo info = espBleParseHidReportMap(buffer, length);
  const EspBleHidReportMapEntry *input = entry(info, hid::ReportIdKeyboard);
  check("6kro not a bitmap", input != nullptr && !input->keyboardBitmap);
  check("6kro modifiers", input != nullptr && input->keyboardHasModifiers);
}

void testNkroKeyboardIsRecognisedAsSuch()
{
  uint8_t buffer[hid::ReportMapCapacity];
  const size_t length =
    hid::compose(buffer, sizeof(buffer), mask(hid::Profile::Keyboard), true, 5, 63);
  // The simple detector is documented as looking for a *boot-compatible 6KRO*
  // report — 8 modifier bits plus at least six 8-bit array entries — so it does
  // not recognise the bitmap form, and that is the contract rather than a gap:
  // it is what a boot-protocol host uses. Anything that wants NKRO has to go
  // through the full parser below. Pinned here because getting this backwards
  // would silently make an NKRO keyboard look like no keyboard at all.
  const EspBleHidKeyboardReportMapInfo keyboard =
    espBleParseKeyboardReportMap(buffer, length);
  check("nkro is not a 6kro keyboard", !keyboard.keyboardFound);

  const EspBleHidReportMapInfo info = espBleParseHidReportMap(buffer, length);
  const EspBleHidReportMapEntry *input = entry(info, hid::ReportIdKeyboard);
  check("nkro entry", input != nullptr);
  if (input == nullptr) return;
  // 1 modifier byte plus a 224-bit usage bitmap: what makes any number of
  // simultaneous keys expressible.
  check("nkro is a bitmap", input->keyboardBitmap);
  check("nkro modifiers", input->keyboardHasModifiers);
  check("nkro modifier offset", input->keyboardModifierBitOffset == 0);
  check("nkro bitmap bits", input->keyboardBitmapBitCount == 224);
  check("nkro bitmap usage minimum", input->keyboardBitmapUsageMinimum == 0);
  check("nkro input size", input->inputByteLength() == 29);
}

void testMouseButtonCountChangesTheDescriptorNotTheReportSize()
{
  for (uint8_t buttons = 1; buttons <= 8; ++buttons)
  {
    uint8_t buffer[hid::ReportMapCapacity];
    const size_t length = hid::compose(
      buffer, sizeof(buffer), mask(hid::Profile::Mouse), false, buttons, 63);
    const EspBleHidReportMapInfo info = espBleParseHidReportMap(buffer, length);
    const EspBleHidReportMapEntry *input = entry(info, hid::ReportIdMouse);
    check("mouse entry", input != nullptr);
    if (input == nullptr) continue;
    check("mouse kind", input->kind == EspBleHidReportKind::Mouse);
    // The buttons and their padding always add up to one byte, so the report stays
    // four bytes wide whatever the button count. A host that had to allocate a
    // different size per configuration would be a compatibility problem.
    check("mouse input size", input->inputByteLength() == 4);
  }
}

void testVendorReportSizeReachesTheInputReport()
{
  const uint8_t sizes[] = {1, 20, 63};
  for (uint8_t size : sizes)
  {
    uint8_t buffer[hid::ReportMapCapacity];
    const size_t length = hid::compose(
      buffer, sizeof(buffer), mask(hid::Profile::Vendor), false, 5, size);
    const EspBleHidReportMapInfo info = espBleParseHidReportMap(buffer, length);
    const EspBleHidReportMapEntry *input = entry(info, hid::ReportIdVendor);
    check("vendor entry", input != nullptr);
    if (input == nullptr) continue;
    check("vendor kind", input->kind == EspBleHidReportKind::Vendor);
    check("vendor input size", input->inputByteLength() == size);
    // The output and feature reports carry the same size; the parser only tracks
    // input reports, so those two are checked in the descriptor itself.
    check("vendor output size",
      buffer[hid::VendorOutputSizeOffset] == size);
    check("vendor feature size",
      buffer[hid::VendorFeatureSizeOffset] == size);
  }
}

void testEveryProfileTogetherKeepsOneReportIdEach()
{
  const uint8_t all = static_cast<uint8_t>(
    mask(hid::Profile::Keyboard) | mask(hid::Profile::Mouse) |
    mask(hid::Profile::Gamepad) | mask(hid::Profile::ConsumerControl) |
    mask(hid::Profile::SystemControl) | mask(hid::Profile::Vendor));
  uint8_t buffer[hid::ReportMapCapacity];
  const size_t length = hid::compose(buffer, sizeof(buffer), all, false, 5, 63);
  check("all profiles fit", length > 0);
  check("all profiles length", length == hid::composedLength(all, false));

  const EspBleHidReportMapInfo info = espBleParseHidReportMap(buffer, length);
  // One HID service carries every profile of a device and the Report ID is what
  // tells them apart, so each has to appear exactly once.
  check("six entries", info.count == hid::ProfileCount);
  const struct { uint8_t reportId; EspBleHidReportKind kind; } expected[] = {
    {hid::ReportIdKeyboard, EspBleHidReportKind::Keyboard},
    {hid::ReportIdMouse, EspBleHidReportKind::Mouse},
    {hid::ReportIdGamepad, EspBleHidReportKind::Gamepad},
    {hid::ReportIdConsumerControl, EspBleHidReportKind::ConsumerControl},
    {hid::ReportIdSystemControl, EspBleHidReportKind::SystemControl},
    {hid::ReportIdVendor, EspBleHidReportKind::Vendor},
  };
  for (const auto &want : expected)
  {
    size_t found = 0;
    for (size_t index = 0; index < info.count; ++index)
    {
      if (info.entries[index].reportId == want.reportId) ++found;
    }
    check("one entry per profile", found == 1);
    check("kind for report id", info.kindForReportId(want.reportId) == want.kind);
  }
  // The concatenation order is part of the wire format: a host reads the maps in
  // profile order, so the keyboard's collection has to come first.
  check("keyboard first", buffer[0] == 0x05 && buffer[1] == 0x01 &&
    buffer[2] == 0x09 && buffer[3] == 0x06);
  // Composing a subset must not shift the report IDs: dropping the keyboard leaves
  // the mouse as report 2, because a host that learned report 2 is a mouse would
  // otherwise be told something else by the same device.
  uint8_t withoutKeyboard[hid::ReportMapCapacity];
  const size_t shortLength = hid::compose(withoutKeyboard,
    sizeof(withoutKeyboard), static_cast<uint8_t>(all & ~mask(hid::Profile::Keyboard)),
    false, 5, 63);
  const EspBleHidReportMapInfo subset =
    espBleParseHidReportMap(withoutKeyboard, shortLength);
  check("subset count", subset.count == hid::ProfileCount - 1);
  check("mouse keeps report 2",
    subset.kindForReportId(hid::ReportIdMouse) == EspBleHidReportKind::Mouse);
}

void testComposeRefusesToOverflow()
{
  uint8_t small[4];
  check("no room", hid::compose(small, sizeof(small),
    mask(hid::Profile::Keyboard), false, 5, 63) == 0);
  check("null buffer", hid::compose(nullptr, 64,
    mask(hid::Profile::Keyboard), false, 5, 63) == 0);
  check("empty mask", hid::compose(small, sizeof(small), 0, false, 5, 63) == 0);
}

void testHidInformationAndPnpIdLayout()
{
  uint8_t information[sizeof(hid::HidInformation)];
  memcpy(information, hid::HidInformation, sizeof(information));
  information[hid::HidInformationCountryOffset] = 0x21;
  // HID 1.11, the country code the caller chose, remote wake.
  check("hid information", information[0] == 0x11 && information[1] == 0x01 &&
    information[2] == 0x21 && information[3] == 0x01);

  uint8_t pnpId[hid::PnpIdLength];
  hid::composePnpId(pnpId, 0x1234, 0xabcd, 0x0102);
  const uint8_t expected[hid::PnpIdLength] =
    {0x02, 0x34, 0x12, 0xcd, 0xab, 0x02, 0x01};
  check("pnp id", memcmp(pnpId, expected, sizeof(expected)) == 0);
}

}  // namespace

int main()
{
  testEachProfileParsesBackToItself();
  testKeyboardCarriesTheLedOutputReport();
  testNkroKeyboardIsRecognisedAsSuch();
  testMouseButtonCountChangesTheDescriptorNotTheReportSize();
  testVendorReportSizeReachesTheInputReport();
  testEveryProfileTogetherKeepsOneReportIdEach();
  testComposeRefusesToOverflow();
  testHidInformationAndPnpIdLayout();
  if (failures == 0) std::printf("PASS\n");
  return failures == 0 ? 0 : 1;
}
