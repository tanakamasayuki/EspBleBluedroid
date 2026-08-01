// Backend-independent codec for the Apple iBeacon advertising layout.
#ifndef ESP_BLE_IBEACON_H
#define ESP_BLE_IBEACON_H

#include <stddef.h>
#include <stdint.h>

static const uint8_t EspBleIBeaconCompanyIdLow = 0x4C;
static const uint8_t EspBleIBeaconCompanyIdHigh = 0x00;
static const uint8_t EspBleIBeaconType = 0x02;
static const uint8_t EspBleIBeaconDataLength = 0x15;
static const size_t EspBleIBeaconManufacturerDataSize = 25;

struct EspBleIBeaconData
{
  uint8_t uuid[16] = {};
  uint16_t major = 0;
  uint16_t minor = 0;
  int8_t measuredPower = 0;
};

inline size_t espBleEncodeIBeacon(
  const EspBleIBeaconData &beacon, uint8_t *out)
{
  out[0] = EspBleIBeaconCompanyIdLow;
  out[1] = EspBleIBeaconCompanyIdHigh;
  out[2] = EspBleIBeaconType;
  out[3] = EspBleIBeaconDataLength;
  for (size_t index = 0; index < 16; ++index)
  {
    out[4 + index] = beacon.uuid[index];
  }
  out[20] = static_cast<uint8_t>((beacon.major >> 8) & 0xff);
  out[21] = static_cast<uint8_t>(beacon.major & 0xff);
  out[22] = static_cast<uint8_t>((beacon.minor >> 8) & 0xff);
  out[23] = static_cast<uint8_t>(beacon.minor & 0xff);
  out[24] = static_cast<uint8_t>(beacon.measuredPower);
  return EspBleIBeaconManufacturerDataSize;
}

inline bool espBleIsIBeacon(
  const uint8_t *manufacturerData, size_t length)
{
  return manufacturerData != nullptr &&
    length >= EspBleIBeaconManufacturerDataSize &&
    manufacturerData[0] == EspBleIBeaconCompanyIdLow &&
    manufacturerData[1] == EspBleIBeaconCompanyIdHigh &&
    manufacturerData[2] == EspBleIBeaconType &&
    manufacturerData[3] == EspBleIBeaconDataLength;
}

inline bool espBleDecodeIBeacon(
  const uint8_t *manufacturerData,
  size_t length,
  EspBleIBeaconData &beacon)
{
  if (!espBleIsIBeacon(manufacturerData, length)) return false;
  for (size_t index = 0; index < 16; ++index)
  {
    beacon.uuid[index] = manufacturerData[4 + index];
  }
  beacon.major = static_cast<uint16_t>(
    (static_cast<uint16_t>(manufacturerData[20]) << 8) |
    manufacturerData[21]);
  beacon.minor = static_cast<uint16_t>(
    (static_cast<uint16_t>(manufacturerData[22]) << 8) |
    manufacturerData[23]);
  beacon.measuredPower = static_cast<int8_t>(manufacturerData[24]);
  return true;
}

#endif // ESP_BLE_IBEACON_H
