// en: CompileSmoke verifies that the library header builds and links on an
//     original ESP32 with the Bluedroid backend. It does not initialize the
//     Bluetooth stack.
// ja: CompileSmokeは無印ESP32のBluedroid構成でライブラリheaderがbuild/link
//     できることを確認する。Bluetooth stackは初期化しない。
#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;
EspBluedroidSppSerial sppSerial(bluetooth);

void setup()
{
  Serial.begin(115200);

  // Keep the common EspBle GAP surface in the compile matrix even though this
  // smoke example intentionally does not start the Bluetooth stack.
  auto &advertising = bluetooth.advertising();
  advertising.data().setName("EspBleBluedroid");
  advertising.data().addServiceUuid("180f");
  const uint8_t serviceData[] = {0x64};
  advertising.data().addServiceData(
    "180f", serviceData, sizeof(serviceData));
  advertising.data().setAppearance(0x0341);
  advertising.data().setTxPowerIncluded(true);
  advertising.scanResponse().setName("EspBleBluedroid");

  EspBleScanResult result;
  String value;
  (void)result.hasServiceData();
  (void)result.hasAppearance();
  (void)result.hasTxPowerLevel();
  (void)result.serviceDataFor("180f", value);

  Serial.printf("EspBleBluedroid %s\n", ESPBLEBLUEDROID_VERSION_STR);
  Serial.printf("SPP Serial connected=%u\n", sppSerial.connected() ? 1 : 0);
}

void loop()
{
  delay(1000);
}
