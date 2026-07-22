#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *SERVICE_UUID =
  "e20ab920-8f4a-4e1d-9003-736563757269";
static constexpr const char *CHARACTERISTIC_UUID =
  "e20ab921-8f4a-4e1d-9003-736563757269";

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

  EspBleConfig invalidSecurity;
  invalidSecurity.security.mitm = true;
  const bool invalidAccepted = bluetooth.begin(invalidSecurity);
  const String invalidError = bluetooth.lastErrorName();

  EspBleConfig unsupportedMitm;
  unsupportedMitm.security.enabled = true;
  unsupportedMitm.security.mitm = true;
  unsupportedMitm.security.ioCapability =
    EspBleSecurityIoCapability::DisplayOnly;
  const bool mitmAccepted = bluetooth.begin(unsupportedMitm);
  const String mitmError = bluetooth.lastErrorName();

  EspBleConfig config;
  config.deviceName = "Bluedroid Security Central";
  config.security.enabled = true;
  config.security.bonding = true;
  config.security.pairOnConnect = true;
  if (!bluetooth.begin(config))
  {
    Serial.printf("SECURITY_INIT_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }
  Serial.printf("DISABLED_SECURITY_OPTIONS_REJECTED %u error=%s\n",
    invalidAccepted ? 0 : 1, invalidError.c_str());
  Serial.printf("MITM_REJECTED %u error=%s\n",
    mitmAccepted ? 0 : 1, mitmError.c_str());

  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    Serial.printf("CENTRAL_CONNECTED id=%u encrypted=%u bonded=%u context=%s\n",
      static_cast<unsigned>(connection.id), connection.encrypted ? 1 : 0,
      connection.bonded ? 1 : 0, contextName());
  });
  bluetooth.onSecurityChanged([](const EspBleSecurityChanged &event) {
    EspBleConnection stored;
    const bool found = bluetooth.connection(event.connection.id, stored);
    Serial.printf(
      "CENTRAL_SECURITY success=%u encrypted=%u authenticated=%u bonded=%u key=%u stored=%u context=%s\n",
      event.success ? 1 : 0, event.connection.encrypted ? 1 : 0,
      event.connection.authenticated ? 1 : 0,
      event.connection.bonded ? 1 : 0, event.connection.encryptionKeySize,
      found && stored.encrypted && stored.bonded ? 1 : 0, contextName());
    if (event.success)
    {
      Serial.printf("DISCOVERY_REQUESTED %u\n",
        bluetooth.discoverServices(event.connection.id, 5000) ? 1 : 0);
    }
  });
  bluetooth.onServicesDiscovered([](const EspBleGattResult &result) {
    Serial.printf("DISCOVERY success=%u context=%s\n",
      result.success ? 1 : 0, contextName());
    if (result.success)
    {
      Serial.printf("SECURE_READ_REQUESTED %u\n",
        bluetooth.readCharacteristic(result.connectionId, SERVICE_UUID,
          CHARACTERISTIC_UUID, 5000) ? 1 : 0);
    }
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    Serial.printf("SECURE_READ success=%u value=%s context=%s\n",
      result.success ? 1 : 0, result.value.c_str(), contextName());
    if (result.success)
    {
      Serial.printf("SECURE_WRITE_REQUESTED %u\n",
        bluetooth.writeCharacteristic(result.connectionId, SERVICE_UUID,
          CHARACTERISTIC_UUID, String("central-secure-write"), true, 5000)
          ? 1 : 0);
    }
  });
  bluetooth.onCharacteristicWritten([](const EspBleGattResult &result) {
    Serial.printf("SECURE_WRITE success=%u context=%s\n",
      result.success ? 1 : 0, contextName());
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf("CENTRAL_DISCONNECTED id=%u encrypted=%u bonded=%u context=%s\n",
      static_cast<unsigned>(connection.id), connection.encrypted ? 1 : 0,
      connection.bonded ? 1 : 0, contextName());
    connectionId = 0;
    connectionRequested = false;
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested || !result.advertisesService(SERVICE_UUID)) return;
    bluetooth.scanner().stop();
    connectionRequested = bluetooth.connect(result);
    Serial.printf("CONNECT_REQUESTED %u\n", connectionRequested ? 1 : 0);
  });
  Serial.println("SECURITY_CENTRAL_READY");
}

void loop()
{
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == 'x')
    {
      const bool cleared = bluetooth.deleteAllBonds();
      Serial.printf("CENTRAL_BONDS_CLEARED success=%u count=%u\n",
        cleared ? 1 : 0, static_cast<unsigned>(bluetooth.bondCount()));
    }
    else if (command == 'b')
    {
      EspBleBond first;
      const size_t count = bluetooth.bondCount();
      const bool listed = count > 0 && bluetooth.bond(0, first) &&
        !first.peerAddress.isEmpty();
      Serial.printf("CENTRAL_BONDS count=%u listed=%u\n",
        static_cast<unsigned>(count), listed ? 1 : 0);
    }
    else if (command == 'y')
    {
      EspBleBond first;
      const bool deleted = bluetooth.bond(0, first) &&
        bluetooth.deleteBond(first);
      Serial.printf("CENTRAL_BOND_DELETED success=%u count=%u\n",
        deleted ? 1 : 0, static_cast<unsigned>(bluetooth.bondCount()));
    }
    else if (command == 's')
    {
      Serial.printf("SCAN_STARTED %u\n",
        bluetooth.scanner().start() ? 1 : 0);
    }
    else if (command == 'd' && connectionId != 0)
    {
      Serial.printf("DISCONNECT_REQUESTED %u\n",
        bluetooth.disconnect(connectionId) ? 1 : 0);
    }
  }
  bluetooth.update();
  delay(1);
}
