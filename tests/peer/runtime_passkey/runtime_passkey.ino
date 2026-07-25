#include <EspBleBluedroid.h>

static constexpr const char *MARKER_SERVICE_UUID = "1815";

EspBleBluedroid bluetooth;
EspBleConnectionId connectionId = 0;
bool connectionRequested = false;

EspBleConfig runtimePasskeyConfig()
{
  EspBleConfig config;
  config.deviceName = "Bluedroid Runtime Central";
  config.security.enabled = true;
  config.security.bonding = true;
  config.security.pairOnConnect = true;
  config.security.mitm = true;
  config.security.ioCapability = EspBleSecurityIoCapability::KeyboardOnly;
  return config;
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  EspBleConfig config = runtimePasskeyConfig();
  if (!bluetooth.begin(config))
  {
    Serial.printf("RUNTIME_PASSKEY_INIT_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }
  const bool invalidPasskeyAccepted = bluetooth.providePasskey(1000000);
  Serial.printf("RUNTIME_PASSKEY_INVALID_REJECTED %u error=%s\n",
    invalidPasskeyAccepted ? 0 : 1, bluetooth.lastErrorName());
  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    Serial.printf("RUNTIME_PASSKEY_CONNECTED id=%u\n",
      static_cast<unsigned>(connection.id));
  });
  bluetooth.onSecurityChanged([](const EspBleSecurityChanged &event) {
    Serial.printf(
      "RUNTIME_PASSKEY_SECURITY success=%u encrypted=%u authenticated=%u bonded=%u key=%u\n",
      event.success ? 1 : 0, event.connection.encrypted ? 1 : 0,
      event.connection.authenticated ? 1 : 0,
      event.connection.bonded ? 1 : 0, event.connection.encryptionKeySize);
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf("RUNTIME_PASSKEY_DISCONNECTED id=%u authenticated=%u\n",
      static_cast<unsigned>(connection.id),
      connection.authenticated ? 1 : 0);
    connectionId = 0;
    connectionRequested = false;
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested || !result.advertisesService(MARKER_SERVICE_UUID))
      return;
    bluetooth.scanner().stop();
    connectionRequested = bluetooth.connect(result);
    Serial.printf("RUNTIME_PASSKEY_CONNECT_REQUESTED %u\n",
      connectionRequested ? 1 : 0);
  });
  Serial.println("RUNTIME_PASSKEY_CENTRAL_READY");
}

void loop()
{
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == 'x')
    {
      const bool cleared = bluetooth.deleteAllBonds();
      Serial.printf("RUNTIME_CENTRAL_BONDS_CLEARED success=%u count=%u\n",
        cleared ? 1 : 0, static_cast<unsigned>(bluetooth.bondCount()));
    }
    else if (command == 's')
    {
      Serial.printf("RUNTIME_PASSKEY_SCAN_STARTED %u\n",
        bluetooth.scanner().start() ? 1 : 0);
    }
    else if (command == 'k')
    {
      const uint32_t passkey = static_cast<uint32_t>(Serial.parseInt());
      Serial.printf("RUNTIME_PASSKEY_PROVIDED accepted=%u passkey=%06u\n",
        bluetooth.providePasskey(passkey) ? 1 : 0,
        static_cast<unsigned>(passkey));
    }
    else if (command == 'd' && connectionId != 0)
    {
      Serial.printf("RUNTIME_PASSKEY_DISCONNECT_REQUESTED %u\n",
        bluetooth.disconnect(connectionId) ? 1 : 0);
    }
    else if (command == 'e')
    {
      const uint32_t startedAt = millis();
      bluetooth.end();
      connectionId = 0;
      connectionRequested = false;
      EspBleConfig config = runtimePasskeyConfig();
      const bool initialized = bluetooth.begin(config);
      Serial.printf("RUNTIME_PASSKEY_END_REINIT success=%u elapsed=%u\n",
        initialized ? 1 : 0,
        static_cast<unsigned>(millis() - startedAt));
    }
  }
  bluetooth.update();
  delay(1);
}
