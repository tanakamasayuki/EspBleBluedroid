#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;

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
    Serial.printf("%s RSSI=%d", result.address.c_str(), result.rssi);
    if (result.hasName())
    {
      Serial.printf(" name=%s", result.name.c_str());
    }
    Serial.println();
  });

  EspBleScanConfig scanConfig;
  scanConfig.active = true;
  scanConfig.intervalMilliseconds = 100;
  scanConfig.windowMilliseconds = 50;
  if (!bluetooth.scanner().start(scanConfig))
  {
    Serial.printf("scan failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
  }
}

void loop()
{
  bluetooth.update();
  delay(1);
}

