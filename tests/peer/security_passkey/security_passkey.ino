#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *SERVICE_UUID =
  "35c6a570-a63d-44a2-9003-706173736b79";
static constexpr const char *CHARACTERISTIC_UUID =
  "35c6a571-a63d-44a2-9003-706173736b79";
static constexpr uint32_t PASSKEY = 438209;

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
EspBleConnectionId connectionId = 0;
bool connectionRequested = false;

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
  config.deviceName = "Bluedroid Passkey Central";
  config.security.enabled = true;
  config.security.bonding = true;
  config.security.pairOnConnect = true;
  config.security.mitm = true;
  config.security.ioCapability = EspBleSecurityIoCapability::DisplayOnly;
  config.security.staticPasskeyEnabled = true;
  config.security.staticPasskey = PASSKEY;
  if (!bluetooth.begin(config))
  {
    Serial.printf("PASSKEY_INIT_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    Serial.printf("PASSKEY_CONNECTED id=%u context=%s\n",
      static_cast<unsigned>(connection.id), contextName());
  });
  bluetooth.onPasskeyDisplayed([](const EspBlePasskeyDisplayed &event) {
    Serial.printf("PASSKEY_DISPLAYED id=%u passkey=%06u context=%s\n",
      static_cast<unsigned>(event.connection.id),
      static_cast<unsigned>(event.passkey), contextName());
  });
  bluetooth.onSecurityChanged([](const EspBleSecurityChanged &event) {
    Serial.printf(
      "PASSKEY_SECURITY success=%u encrypted=%u authenticated=%u bonded=%u key=%u context=%s\n",
      event.success ? 1 : 0, event.connection.encrypted ? 1 : 0,
      event.connection.authenticated ? 1 : 0,
      event.connection.bonded ? 1 : 0, event.connection.encryptionKeySize,
      contextName());
    if (event.success)
    {
      Serial.printf("PASSKEY_DISCOVERY_REQUESTED %u\n",
        bluetooth.discoverServices(event.connection.id, 5000) ? 1 : 0);
    }
  });
  bluetooth.onServicesDiscovered([](const EspBleGattResult &result) {
    Serial.printf("PASSKEY_DISCOVERY success=%u context=%s\n",
      result.success ? 1 : 0, contextName());
    if (result.success)
    {
      Serial.printf("AUTH_READ_REQUESTED %u\n",
        bluetooth.readCharacteristic(result.connectionId, SERVICE_UUID,
          CHARACTERISTIC_UUID, 5000) ? 1 : 0);
    }
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    Serial.printf("AUTH_READ success=%u value=%s context=%s\n",
      result.success ? 1 : 0, result.value.c_str(), contextName());
    if (result.success)
    {
      Serial.printf("AUTH_WRITE_REQUESTED %u\n",
        bluetooth.writeCharacteristic(result.connectionId, SERVICE_UUID,
          CHARACTERISTIC_UUID, String("authenticated-write"), true, 5000)
          ? 1 : 0);
    }
  });
  bluetooth.onCharacteristicWritten([](const EspBleGattResult &result) {
    Serial.printf("AUTH_WRITE success=%u context=%s\n",
      result.success ? 1 : 0, contextName());
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf("PASSKEY_DISCONNECTED id=%u authenticated=%u context=%s\n",
      static_cast<unsigned>(connection.id),
      connection.authenticated ? 1 : 0, contextName());
    connectionId = 0;
    connectionRequested = false;
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested || !result.advertisesService(SERVICE_UUID)) return;
    bluetooth.scanner().stop();
    connectionRequested = bluetooth.connect(result);
    Serial.printf("PASSKEY_CONNECT_REQUESTED %u\n",
      connectionRequested ? 1 : 0);
  });
  Serial.println("PASSKEY_CENTRAL_READY");
}

void loop()
{
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == 'x')
    {
      const bool cleared = bluetooth.deleteAllBonds();
      Serial.printf("PASSKEY_CENTRAL_BONDS_CLEARED success=%u count=%u\n",
        cleared ? 1 : 0, static_cast<unsigned>(bluetooth.bondCount()));
    }
    else if (command == 'b')
    {
      Serial.printf("PASSKEY_CENTRAL_BONDS count=%u\n",
        static_cast<unsigned>(bluetooth.bondCount()));
    }
    else if (command == 's')
    {
      Serial.printf("PASSKEY_SCAN_STARTED %u\n",
        bluetooth.scanner().start() ? 1 : 0);
    }
    else if (command == 'd' && connectionId != 0)
    {
      Serial.printf("PASSKEY_DISCONNECT_REQUESTED %u\n",
        bluetooth.disconnect(connectionId) ? 1 : 0);
    }
  }
  bluetooth.update();
  delay(1);
}
