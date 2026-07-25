#include <EspBleBluedroid.h>

static constexpr const char *AUTHENTICATED_SERVICE_UUID =
  "35c6a570-a63d-44a2-9003-706173736b79";

EspBleBluedroid bluetooth;
bool connectionRequested = false;

void setup()
{
  Serial.begin(115200);
  Serial.setTimeout(30000);

  EspBleConfig config;
  config.deviceName = "Bluedroid Passkey Input";
  config.security.enabled = true;
  config.security.bonding = true;
  config.security.pairOnConnect = true;
  config.security.mitm = true;
  config.security.ioCapability = EspBleSecurityIoCapability::KeyboardOnly;
  if (!bluetooth.begin(config)) return;

  bluetooth.onConnected([](const EspBleConnection &connection) {
    Serial.printf("Connected: %u\n", static_cast<unsigned>(connection.id));
    Serial.println("Enter the peer's six-digit passkey:");
  });
  bluetooth.onSecurityChanged([](const EspBleSecurityChanged &event) {
    Serial.printf("Authenticated: %u, bonded: %u\n",
      event.connection.authenticated ? 1 : 0,
      event.connection.bonded ? 1 : 0);
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested ||
        !result.advertisesService(AUTHENTICATED_SERVICE_UUID)) return;
    bluetooth.scanner().stop();
    connectionRequested = bluetooth.connect(result);
  });
  bluetooth.scanner().start();
}

void loop()
{
  if (Serial.available())
  {
    const uint32_t passkey = static_cast<uint32_t>(Serial.parseInt());
    if (!bluetooth.providePasskey(passkey))
    {
      Serial.printf("Passkey rejected: %s\n",
        bluetooth.lastErrorDetail().c_str());
    }
  }
  bluetooth.update();
  delay(1);
}
