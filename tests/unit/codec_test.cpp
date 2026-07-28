#include "EspBleBluedroidCodec.h"
#include "EspBleBluedroidGattcState.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

using espblebluedroid::internal::BleUuid;
using espblebluedroid::internal::LegacyAdvertisingData;
using espblebluedroid::internal::formatBleAddress;
using espblebluedroid::internal::formatBleUuid;
using espblebluedroid::internal::GattcLinkState;
using espblebluedroid::internal::GattcOpenResult;
using espblebluedroid::internal::GattcState;
using espblebluedroid::internal::parseBleAddress;
using espblebluedroid::internal::parseBleUuid;
using espblebluedroid::internal::uuidEquals;

namespace
{
void testAddressCodec()
{
  uint8_t address[6] = {};
  assert(parseBleAddress("01:23:45:67:89:AB", address));
  const uint8_t expected[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab};
  assert(std::memcmp(address, expected, sizeof(expected)) == 0);
  assert(formatBleAddress(address) == "01:23:45:67:89:ab");

  assert(!parseBleAddress(nullptr, address));
  assert(!parseBleAddress("", address));
  assert(!parseBleAddress("01:23:45:67:89", address));
  assert(!parseBleAddress("01:23:45:67:89:xyz", address));
  assert(!parseBleAddress("01-23-45-67-89-ab", address));
}

void testUuidCodec()
{
  BleUuid uuid;
  assert(parseBleUuid("180D", uuid));
  assert(uuid.bitSize == 16);
  assert(uuid.bytes[0] == 0x0d);
  assert(uuid.bytes[1] == 0x18);
  assert(formatBleUuid(uuid) == "0000180d-0000-1000-8000-00805f9b34fb");

  assert(parseBleUuid("12345678", uuid));
  assert(uuid.bitSize == 32);
  const uint8_t expected32[] = {0x78, 0x56, 0x34, 0x12};
  assert(std::memcmp(uuid.bytes.data(), expected32, sizeof(expected32)) == 0);
  assert(formatBleUuid(uuid) == "12345678-0000-1000-8000-00805f9b34fb");

  assert(parseBleUuid("12345678-1234-5678-9abc-def012345678", uuid));
  assert(uuid.bitSize == 128);
  const uint8_t expected128[] = {
    0x78, 0x56, 0x34, 0x12, 0xf0, 0xde, 0xbc, 0x9a,
    0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12};
  assert(std::memcmp(uuid.bytes.data(), expected128, sizeof(expected128)) == 0);
  assert(
    formatBleUuid(uuid) ==
    "12345678-1234-5678-9abc-def012345678");

  BleUuid invalid;
  assert(formatBleUuid(invalid).empty());

  assert(uuidEquals(
    "180d", "0000180D-0000-1000-8000-00805F9B34FB"));
  assert(uuidEquals(
    "12345678", "12345678-0000-1000-8000-00805f9b34fb"));
  assert(!uuidEquals("180d", "180f"));
  assert(!uuidEquals("", "180d"));
  assert(!uuidEquals("not-a-uuid", "180d"));

  assert(!parseBleUuid(nullptr, uuid));
  assert(!parseBleUuid("180", uuid));
  assert(!parseBleUuid("gggg", uuid));
  assert(!parseBleUuid("12345678-1234-5678-9abc-def01234567", uuid));
  assert(!parseBleUuid("123456781234-5678-9abc-def012345678", uuid));
}

void testLegacyAdvertisingData()
{
  LegacyAdvertisingData payload;
  const uint8_t flags[] = {0x06};
  assert(payload.append(0x01, flags, sizeof(flags)));
  assert(payload.size() == 3);
  assert(payload.data()[0] == 2);
  assert(payload.data()[1] == 0x01);
  assert(payload.data()[2] == 0x06);

  const std::array<uint8_t, 26> name = {};
  assert(payload.append(0x09, name.data(), name.size()));
  assert(payload.size() == 31);
  assert(!payload.append(0x0a, flags, sizeof(flags)));
  assert(payload.size() == 31);

  LegacyAdvertisingData invalid;
  assert(!invalid.append(0x09, nullptr, 1));
  assert(invalid.append(0x09, nullptr, 0));
}

void testGattcConnectionState()
{
  GattcState state;
  assert(state.linkState() == GattcLinkState::Unregistered);
  assert(state.beginRegistration());
  assert(!state.beginRegistration());
  assert(state.registered(3));
  assert(state.gattcIf() == 3);
  assert(state.linkState() == GattcLinkState::Idle);

  const uint32_t firstAttempt = state.beginOpen();
  assert(firstAttempt != 0);
  assert(state.linkState() == GattcLinkState::Opening);
  assert(state.beginOpen() == 0);
  assert(state.beginCancel(firstAttempt));
  assert(state.linkState() == GattcLinkState::Cancelling);
  assert(
    state.opened(firstAttempt, 7) ==
    GattcOpenResult::ConnectedAfterCancel);
  assert(state.linkState() == GattcLinkState::Closing);
  assert(state.connectionId() == 7);
  assert(!state.cancelled(firstAttempt));
  assert(state.disconnected(7));
  assert(state.linkState() == GattcLinkState::Idle);

  const uint32_t secondAttempt = state.beginOpen();
  assert(secondAttempt != 0 && secondAttempt != firstAttempt);
  assert(state.opened(firstAttempt, 8) == GattcOpenResult::Ignored);
  assert(state.opened(secondAttempt, 9) == GattcOpenResult::Connected);
  assert(state.connectionId() == 9);
  assert(state.linkState() == GattcLinkState::Connected);
  assert(state.beginClose(8) == false);
  assert(state.beginClose(9));
  assert(state.linkState() == GattcLinkState::Closing);
  assert(!state.disconnected(8));
  assert(state.disconnected(9));
  assert(state.linkState() == GattcLinkState::Idle);

  const uint32_t thirdAttempt = state.beginOpen();
  assert(thirdAttempt != 0);
  assert(state.openFailed(thirdAttempt));
  assert(state.linkState() == GattcLinkState::Idle);

  state.reset();
  assert(state.linkState() == GattcLinkState::Unregistered);
  assert(state.gattcIf() == 0xff);
  assert(state.opened(thirdAttempt, 10) == GattcOpenResult::Ignored);
}
} // namespace

int main()
{
  testAddressCodec();
  testUuidCodec();
  testLegacyAdvertisingData();
  testGattcConnectionState();
  return 0;
}
