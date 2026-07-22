#include <Arduino.h>
#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID =
  "8d47a630-8d3a-4d65-a76f-6f626c756564";

EspBleBluedroid bluetooth;
static bool found = false;
static bool prebeginRejected = false;
static bool scanStarted = false;
static bool updatesEnabled = false;
static uint32_t enableUpdatesAt = 0;

void setup()
{
  Serial.begin(115200);
  delay(500);

  if (!bluetooth.scanner().start() &&
      bluetooth.lastError() == EspBleError::InvalidState)
  {
    prebeginRejected = true;
  }

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Central";
  if (!bluetooth.begin(config) || !bluetooth.initialized())
  {
    Serial.printf("BEGIN_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  if (!bluetooth.begin(config))
  {
    Serial.println("IDEMPOTENT_BEGIN_FAILED");
    return;
  }

  auto &advertising = bluetooth.advertising();
  uint8_t oversizedManufacturerData[29] = {};
  advertising.setScanResponseEnabled(false);
  advertising.setManufacturerData(
    oversizedManufacturerData, sizeof(oversizedManufacturerData));
  const bool oversizedStarted = advertising.start();
  if (!oversizedStarted &&
      bluetooth.lastError() == EspBleError::InvalidArgument)
  {
    Serial.println("OVERSIZE_REJECTED");
  }
  else
  {
    Serial.println("OVERSIZE_NOT_REJECTED");
    if (oversizedStarted)
    {
      advertising.stop();
    }
  }
  advertising.clear();

  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (found || !result.advertisesService(SERVICE_UUID))
    {
      return;
    }
    found = true;
    Serial.printf(
      "SCAN_RESULT name=%s mfg=%02x%02x rssi=%d connectable=%u scannable=%u\n",
      result.name.c_str(),
      result.manufacturerData.length() > 0
        ? static_cast<uint8_t>(result.manufacturerData[0]) : 0,
      result.manufacturerData.length() > 1
        ? static_cast<uint8_t>(result.manufacturerData[1]) : 0,
      result.rssi,
      result.connectable ? 1 : 0,
      result.scannable ? 1 : 0);
    bluetooth.scanner().stop();
  });

  Serial.println("CENTRAL_COMMAND_READY");
}

void loop()
{
  if (!scanStarted && Serial.available() && Serial.read() == 's')
  {
    EspBleScanConfig scanConfig;
    scanConfig.active = true;
    scanConfig.intervalMilliseconds = 100;
    scanConfig.windowMilliseconds = 50;
    if (!bluetooth.scanner().start(scanConfig))
    {
      Serial.printf("SCAN_START_FAILED %s\n", bluetooth.lastErrorName());
      return;
    }
    scanStarted = true;
    enableUpdatesAt = millis() + 2000;
    Serial.println("SCAN_STARTED_NO_UPDATE");
  }
  if (scanStarted && !updatesEnabled &&
      static_cast<int32_t>(millis() - enableUpdatesAt) >= 0)
  {
    updatesEnabled = true;
    if (prebeginRejected)
    {
      Serial.println("PREBEGIN_REJECTED");
    }
    Serial.println("UPDATE_START");
  }
  if (updatesEnabled)
  {
    bluetooth.update();
  }
  delay(1);
}
