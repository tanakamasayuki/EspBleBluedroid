#include <EspBleBluedroid.h>

static constexpr const char *SECURE_SERVICE_UUID =
  "e20ab920-8f4a-4e1d-9003-736563757269";

EspBleBluedroid bluetooth;
bool connectionRequested = false;

void setup()
{
  Serial.begin(115200);
  EspBleConfig config;
  config.deviceName = "Bluedroid Secure Central";
  config.security.enabled = true;
  config.security.bonding = true;
  config.security.pairOnConnect = true;
  if (!bluetooth.begin(config)) return;

  bluetooth.onConnected([](const EspBleConnection &connection) {
    Serial.printf("Connected: %u\n", static_cast<unsigned>(connection.id));
  });
  bluetooth.onSecurityChanged([](const EspBleSecurityChanged &event) {
    Serial.printf("Security: success=%u encrypted=%u bonded=%u key=%u\n",
      event.success ? 1 : 0, event.connection.encrypted ? 1 : 0,
      event.connection.bonded ? 1 : 0,
      event.connection.encryptionKeySize);
    EspBleBond firstBond;
    if (bluetooth.bondCount() > 0 && bluetooth.bond(0, firstBond))
    {
      Serial.printf("Stored bond: %s\n", firstBond.peerAddress.c_str());
    }
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested ||
        !result.advertisesService(SECURE_SERVICE_UUID)) return;
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
