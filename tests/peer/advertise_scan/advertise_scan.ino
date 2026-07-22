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
static uint16_t callbackCount = 0;

EspBleConfig makeConfig()
{
  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Central";
  return config;
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  if (!bluetooth.scanner().start() &&
      bluetooth.lastError() == EspBleError::InvalidState)
  {
    prebeginRejected = true;
  }

  const EspBleConfig config = makeConfig();
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
    ++callbackCount;
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
  if (Serial.available())
  {
    const char command = Serial.read();
    if (!scanStarted && command == 's')
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
    else if (command == 't')
    {
      EspBleScanConfig scanConfig;
      scanConfig.active = true;
      scanConfig.durationSeconds = 1;
      scanConfig.intervalMilliseconds = 100;
      scanConfig.windowMilliseconds = 50;
      const bool durationStarted = bluetooth.scanner().start(scanConfig);
      delay(1500);
      const bool durationStopped = !bluetooth.scanner().isScanning();
      bluetooth.update();
      Serial.printf("DURATION_SCAN started=%u stopped=%u\n",
        durationStarted ? 1 : 0, durationStopped ? 1 : 0);

      scanConfig.durationSeconds = 0;
      const bool explicitStarted = bluetooth.scanner().start(scanConfig);
      const bool explicitStopped = bluetooth.scanner().stop() &&
        !bluetooth.scanner().isScanning();
      Serial.printf("EXPLICIT_STOP started=%u stopped=%u\n",
        explicitStarted ? 1 : 0, explicitStopped ? 1 : 0);

      scanConfig.wantDuplicates = true;
      const uint16_t callbacksBeforeEnd = callbackCount;
      const bool endScanStarted = bluetooth.scanner().start(scanConfig);
      delay(700);
      bluetooth.end();
      const bool ended = !bluetooth.initialized() &&
        !bluetooth.scanner().isScanning();
      const bool beganAgain = bluetooth.begin(makeConfig());
      bluetooth.update();
      Serial.printf("END_SCAN started=%u ended=%u reinitialized=%u stale=%u\n",
        endScanStarted ? 1 : 0, ended ? 1 : 0, beganAgain ? 1 : 0,
        static_cast<unsigned>(callbackCount - callbacksBeforeEnd));
    }
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
