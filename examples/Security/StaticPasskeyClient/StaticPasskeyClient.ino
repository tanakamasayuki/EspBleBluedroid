#include <EspBleBluedroid.h>

static constexpr const char *AUTHENTICATED_SERVICE_UUID =
  "35c6a570-a63d-44a2-9003-706173736b79";
static constexpr uint32_t PASSKEY = 438209;

EspBleBluedroid bluetooth;
bool connectionRequested = false;

void setup()
{
  Serial.begin(115200);
  EspBleConfig config;
  config.deviceName = "Bluedroid Passkey Central";
  config.security.enabled = true;
  config.security.bonding = true;
  config.security.pairOnConnect = true;
  config.security.mitm = true;
  config.security.ioCapability = EspBleSecurityIoCapability::DisplayOnly;
  config.security.staticPasskeyEnabled = true;
  config.security.staticPasskey = PASSKEY;
  if (!bluetooth.begin(config)) return;

  bluetooth.onPasskeyDisplayed([](const EspBlePasskeyDisplayed &event) {
    Serial.printf("Enter passkey on peer: %06u\n",
      static_cast<unsigned>(event.passkey));
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
  bluetooth.update();
  delay(1);
}
