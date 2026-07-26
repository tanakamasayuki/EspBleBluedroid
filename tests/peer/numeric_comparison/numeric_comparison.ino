#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *MARKER_SERVICE_UUID = "1815";

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
EspBleConnectionId connectionId = 0;
bool connectionRequested = false;
bool timeoutTestActive = false;
uint32_t timeoutStartedAt = 0;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();

  EspBleConfig config;
  config.deviceName = "Bluedroid NumCmp Central";
  config.security.enabled = true;
  config.security.bonding = true;
  config.security.pairOnConnect = true;
  config.security.mitm = true;
  config.security.ioCapability = EspBleSecurityIoCapability::DisplayYesNo;
  if (!bluetooth.begin(config))
  {
    Serial.printf("NUMCMP_INIT_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    Serial.printf("NUMCMP_CENTRAL_CONNECTED id=%u\n",
      static_cast<unsigned>(connection.id));
  });
  bluetooth.onNumericComparison([](const EspBlePasskeyDisplayed &event) {
    if (timeoutTestActive) timeoutStartedAt = millis();
    Serial.printf("NUMCMP_CENTRAL_VALUE id=%u value=%06u context=%s\n",
      static_cast<unsigned>(event.connection.id),
      static_cast<unsigned>(event.passkey), contextName());
  });
  bluetooth.onSecurityChanged([](const EspBleSecurityChanged &event) {
    Serial.printf(
      "NUMCMP_CENTRAL_SECURITY success=%u encrypted=%u authenticated=%u bonded=%u key=%u context=%s\n",
      event.success ? 1 : 0, event.connection.encrypted ? 1 : 0,
      event.connection.authenticated ? 1 : 0,
      event.connection.bonded ? 1 : 0, event.connection.encryptionKeySize,
      contextName());
    if (timeoutTestActive)
    {
      Serial.printf("NUMCMP_CENTRAL_TIMEOUT success=%u elapsed=%u\n",
        event.success ? 1 : 0,
        static_cast<unsigned>(millis() - timeoutStartedAt));
      timeoutTestActive = false;
    }
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf("NUMCMP_CENTRAL_DISCONNECTED id=%u context=%s\n",
      static_cast<unsigned>(connection.id), contextName());
    connectionId = 0;
    connectionRequested = false;
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested || !result.advertisesService(MARKER_SERVICE_UUID))
      return;
    bluetooth.scanner().stop();
    connectionRequested = bluetooth.connect(result);
    Serial.printf("NUMCMP_CONNECT_REQUESTED %u\n",
      connectionRequested ? 1 : 0);
  });
  Serial.println("NUMCMP_CENTRAL_READY");
}

void loop()
{
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == 'x')
    {
      const bool cleared = bluetooth.deleteAllBonds();
      Serial.printf("NUMCMP_CENTRAL_BONDS_CLEARED success=%u count=%u\n",
        cleared ? 1 : 0, static_cast<unsigned>(bluetooth.bondCount()));
    }
    else if (command == 's')
    {
      Serial.printf("NUMCMP_SCAN_STARTED %u\n",
        bluetooth.scanner().start() ? 1 : 0);
    }
    else if (command == 'y' || command == 'n')
    {
      Serial.printf("NUMCMP_CENTRAL_CONFIRM accepted=%u\n",
        bluetooth.confirmNumericComparison(command == 'y') ? 1 : 0);
    }
    else if (command == 'd' && connectionId != 0)
    {
      Serial.printf("NUMCMP_DISCONNECT_REQUESTED %u\n",
        bluetooth.disconnect(connectionId) ? 1 : 0);
    }
#ifdef ESP_BLE_BLUEDROID_TESTING
    else if (command == 'o')
    {
      timeoutTestActive = true;
      Serial.printf("NUMCMP_CENTRAL_TIMEOUT_SET %u\n",
        bluetooth.setSecurityResponseTimeoutForTest(250) ? 1 : 0);
    }
#endif
  }
  bluetooth.update();
  delay(1);
}
