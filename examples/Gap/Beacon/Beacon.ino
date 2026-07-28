// en: Beacon - broadcast non-connectable, non-scannable manufacturer data.
// ja: Beacon - non-connectable・non-scannableなManufacturer Dataを放送する。
#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;

// en: Company ID 0xFFFF is reserved for testing. Replace it in a product.
// ja: Company ID 0xFFFFはテスト用。製品では割り当て済みIDへ置き換える。
static const uint8_t manufacturerData[] = {
  0xff, 0xff, 0x01, 0x02, 0x03, 0x04};

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Beacon";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE init failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  auto &advertising = bluetooth.advertising();
  advertising.setConnectable(false);
  advertising.setScanResponseEnabled(false);
  advertising.setManufacturerData(
    manufacturerData, sizeof(manufacturerData));
  advertising.setInterval(100, 150);
  if (!advertising.start())
  {
    Serial.printf("Advertising failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
  }
}

void loop()
{
  bluetooth.update();
  delay(1);
}
