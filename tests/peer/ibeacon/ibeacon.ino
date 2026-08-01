#include <EspBleBluedroid.h>
#include <EspBleIBeacon.h>

EspBleBluedroid bluetooth;
bool reported = false;

void setup()
{
  Serial.begin(115200);
  delay(500);
  if (!bluetooth.begin())
  {
    Serial.printf("BLE_INIT_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (reported || !result.hasManufacturerData()) return;
    EspBleIBeaconData beacon;
    if (!espBleDecodeIBeacon(
          reinterpret_cast<const uint8_t *>(
            result.manufacturerData.c_str()),
          result.manufacturerData.length(), beacon))
    {
      return;
    }
    reported = true;
    bluetooth.scanner().stop();
    char uuidHex[33];
    for (size_t index = 0; index < 16; ++index)
    {
      snprintf(uuidHex + index * 2, 3, "%02x", beacon.uuid[index]);
    }
    Serial.printf(
      "IBEACON uuid=%s major=%u minor=%u power=%d connectable=%u scannable=%u\n",
      uuidHex,
      static_cast<unsigned>(beacon.major),
      static_cast<unsigned>(beacon.minor),
      static_cast<int>(beacon.measuredPower),
      result.connectable ? 1 : 0,
      result.scannable ? 1 : 0);
  });
}

void loop()
{
  if (Serial.available() > 0 && Serial.read() == 's')
  {
    reported = false;
    Serial.println(bluetooth.scanner().start()
      ? "SCAN_STARTED" : "SCAN_START_FAILED");
  }
  bluetooth.update();
  delay(1);
}
