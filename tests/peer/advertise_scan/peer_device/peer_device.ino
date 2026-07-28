#include <Arduino.h>
#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID =
  "180d";

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);
  delay(500);

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Peripheral";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BEGIN_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }

  auto &advertising = bluetooth.advertising();
  auto &data = advertising.data();
  data.setAppearance(0x0341);
  data.setTxPowerIncluded(true);
  if (!data.addServiceUuid(SERVICE_UUID))
  {
    Serial.printf("UUID_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  const uint8_t manufacturerData[] = {0x34, 0x12};
  data.setManufacturerData(manufacturerData, sizeof(manufacturerData));
  const uint8_t serviceData[] = {0xa5, 0x00, 0x5a};
  if (!data.addServiceData("180f", serviceData, sizeof(serviceData)))
  {
    Serial.println("SERVICE_DATA_FAILED");
    return;
  }
  advertising.scanResponse().setName("Bluedroid Response");
  advertising.setScanResponseEnabled(true);
  if (!advertising.start())
  {
    Serial.printf("ADVERTISE_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }

  Serial.println("PERIPHERAL_ADVERTISING");
}

void loop()
{
  bluetooth.update();
  delay(1);
}
