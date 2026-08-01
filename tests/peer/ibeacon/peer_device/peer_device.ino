#include <EspBleBluedroid.h>
#include <EspBleIBeacon.h>

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);
  delay(500);
  if (!bluetooth.begin()) return;

  EspBleIBeaconData beacon;
  const uint8_t uuid[16] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
  memcpy(beacon.uuid, uuid, sizeof(uuid));
  beacon.major = 0x1234;
  beacon.minor = 0xabcd;
  beacon.measuredPower = -59;
  uint8_t payload[EspBleIBeaconManufacturerDataSize];
  espBleEncodeIBeacon(beacon, payload);

  auto &advertising = bluetooth.advertising();
  advertising.setConnectable(false);
  advertising.setScanResponseEnabled(false);
  advertising.setManufacturerData(payload, sizeof(payload));
  advertising.setInterval(100, 150);
  advertising.start();
}

void loop()
{
  if (Serial.available() > 0 && Serial.read() == '?')
  {
    Serial.printf("ADVERTISING %u\n",
      bluetooth.advertising().isAdvertising() ? 1 : 0);
  }
  bluetooth.update();
  delay(1);
}
