#include <Arduino.h>
#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID =
  "8d47a630-8d3a-4d65-a76f-6f626c756564";

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
  advertising.setName("Bluedroid Peer");
  if (!advertising.addServiceUuid(SERVICE_UUID))
  {
    Serial.printf("UUID_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  const uint8_t manufacturerData[] = {0x34, 0x12};
  advertising.setManufacturerData(manufacturerData, sizeof(manufacturerData));
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

