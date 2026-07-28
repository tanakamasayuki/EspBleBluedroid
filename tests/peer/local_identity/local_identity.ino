#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID =
  "70726976-6163-7900-9003-72616e646d01";

EspBleBluedroid bluetooth;
bool reported = false;

void setup()
{
  Serial.begin(115200);
  delay(500);

  if (!bluetooth.begin())
  {
    Serial.printf("BEGIN_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (reported || !result.advertisesService(SERVICE_UUID)) return;
    reported = true;
    bluetooth.scanner().stop();
    Serial.printf(
      "OBSERVED address=%s type=%u txpower=%s\n",
      result.address.c_str(),
      static_cast<unsigned>(result.addressType),
      result.hasTxPowerLevel()
        ? String(result.txPowerLevel).c_str() : "-");
  });
  Serial.println("OBSERVER_READY");
}

void loop()
{
  if (Serial.available() > 0 && Serial.read() == 's')
  {
    reported = false;
    EspBleScanConfig config;
    config.active = true;
    Serial.println(
      bluetooth.scanner().start(config)
        ? "SCAN_STARTED" : "SCAN_START_FAILED");
  }

  bluetooth.update();
  delay(1);
}
