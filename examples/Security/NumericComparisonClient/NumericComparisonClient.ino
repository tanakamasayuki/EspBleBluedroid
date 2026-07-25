#include <EspBleBluedroid.h>

static constexpr const char *AUTHENTICATED_SERVICE_UUID =
  "35c6a570-a63d-44a2-9003-706173736b79";

EspBleBluedroid bluetooth;
bool connectionRequested = false;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "Bluedroid NumCmp Client";
  config.security.enabled = true;
  config.security.bonding = true;
  config.security.pairOnConnect = true;
  config.security.mitm = true;
  config.security.ioCapability = EspBleSecurityIoCapability::DisplayYesNo;
  if (!bluetooth.begin(config)) return;

  bluetooth.onNumericComparison([](const EspBlePasskeyDisplayed &event) {
    Serial.printf("Does the peer display %06u? Enter y or n.\n",
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
  if (Serial.available())
  {
    const char answer = Serial.read();
    if (answer == 'y' || answer == 'n')
    {
      bluetooth.confirmNumericComparison(answer == 'y');
    }
  }
  bluetooth.update();
  delay(1);
}
