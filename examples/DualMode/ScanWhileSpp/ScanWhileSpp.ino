// en: ScanWhileSpp - run BLE and Bluetooth Classic at the same time. Connect an
//     SPP session, then scan BLE for ten seconds without dropping it. One object
//     owns one dual-mode stack, but the two transports stay separate APIs with
//     separate result types: BLE at the root, Classic under classic().
// ja: ScanWhileSpp - BLEとBluetooth Classicを同時に動かす。SPP sessionを接続し、
//     それを維持したまま10秒間BLE Scanする。1つのオブジェクトが1つのdual mode stackを
//     所有するが、2つのトランスポートはAPIも結果型も別のまま。BLEはroot直下、Classicは
//     classic() 配下。
#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;
EspBluedroidSppSessionId sppSessionId = 0;

void setup()
{
  Serial.begin(115200);
  if (!bluetooth.begin())
  {
    Serial.printf("begin failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    Serial.printf("BLE %s RSSI=%d while SPP session %u is active\n",
      result.address.c_str(), result.rssi,
      static_cast<unsigned>(sppSessionId));
  });
  bluetooth.classic().spp().onConnected(
    [](const EspBluedroidSppSession &session) {
      sppSessionId = session.id;
      EspBleScanConfig scanConfig;
      scanConfig.durationSeconds = 10;
      if (!bluetooth.scanner().start(scanConfig))
      {
        Serial.printf("scan failed: %s\n", bluetooth.lastErrorName());
      }
    });
  bluetooth.classic().spp().onDisconnected(
    [](const EspBluedroidSppSession &) {
      sppSessionId = 0;
      if (bluetooth.scanner().isScanning()) bluetooth.scanner().stop();
    });
  bluetooth.classic().spp().onConnectionFailed(
    [](const EspBluedroidSppConnectionFailure &failure) {
      Serial.printf("SPP connect failed: %s\n", failure.detail.c_str());
    });

  Serial.println("Enter the Classic address of an SPP Server");
}

void loop()
{
  if (Serial.available() && sppSessionId == 0)
  {
    const String address = Serial.readStringUntil('\n');
    if (!bluetooth.classic().spp().connect(address.c_str()))
    {
      Serial.printf("request rejected: %s\n", bluetooth.lastErrorName());
    }
  }
  bluetooth.update();
  delay(1);
}
