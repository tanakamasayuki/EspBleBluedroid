#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Advertiser";
  if (!bluetooth.begin(config))
  {
    Serial.printf("begin failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  auto &advertising = bluetooth.advertising();
  advertising.setName("Bluedroid Advertiser");
  advertising.addServiceUuid("180f"); // Battery Service
  const uint8_t manufacturerData[] = {0x34, 0x12, 0x01};
  advertising.setManufacturerData(manufacturerData, sizeof(manufacturerData));
  advertising.setScanResponseEnabled(true);
  if (!advertising.start())
  {
    Serial.printf("advertising failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }
  Serial.println("advertising");
}

void loop()
{
  bluetooth.update();
  delay(1);
}

