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
