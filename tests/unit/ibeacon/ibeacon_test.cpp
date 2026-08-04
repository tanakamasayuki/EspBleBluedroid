#include "EspBleIBeacon.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

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
} // namespace

int main()
{
  EspBleIBeaconData beacon;
  const uint8_t uuid[16] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
  std::memcpy(beacon.uuid, uuid, sizeof(uuid));
  beacon.major = 0x1234;
  beacon.minor = 0xabcd;
  beacon.measuredPower = -59;

  uint8_t output[EspBleIBeaconManufacturerDataSize] = {};
  check("encoded size", espBleEncodeIBeacon(beacon, output) == 25);
  check("header", output[0] == 0x4c && output[1] == 0x00 &&
    output[2] == 0x02 && output[3] == 0x15);
  check("uuid", std::memcmp(output + 4, uuid, sizeof(uuid)) == 0);
  check("major", output[20] == 0x12 && output[21] == 0x34);
  check("minor", output[22] == 0xab && output[23] == 0xcd);
  check("power", output[24] == 0xc5);
  check("recognize", espBleIsIBeacon(output, sizeof(output)));

  EspBleIBeaconData decoded;
  check("decode", espBleDecodeIBeacon(output, sizeof(output), decoded));
  check("round trip uuid", std::memcmp(decoded.uuid, uuid, sizeof(uuid)) == 0);
  check("round trip values", decoded.major == beacon.major &&
    decoded.minor == beacon.minor &&
    decoded.measuredPower == beacon.measuredPower);
  check("reject short", !espBleIsIBeacon(output, 10));

  if (failures == 0) std::printf("OK\n");
  return failures == 0 ? 0 : 1;
}
