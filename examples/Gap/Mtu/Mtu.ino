#include <EspBleBluedroid.h>

static constexpr const char *TARGET_SERVICE_UUID = "180f";

EspBleBluedroid bluetooth;
bool connectionRequested = false;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid MTU Central";
  config.preferredMtu = 185;
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE init failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    Serial.printf("Connected with initial MTU %u\n", connection.mtu);
  });
  bluetooth.onMtuChanged([](const EspBleMtuChanged &event) {
    Serial.printf(
      "MTU changed from %u to %u (notification payload up to %u bytes)\n",
      event.previousMtu,
      event.connection.mtu,
      static_cast<unsigned>(
        event.connection.maximumNotificationPayload()));
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf("Disconnected: reason=%d\n", connection.disconnectReason);
    connectionRequested = false;
  });
  bluetooth.onConnectionFailed([](const EspBleConnectionFailure &failure) {
    Serial.printf("Connection failed: %s (%s)\n",
      failure.peerAddress.c_str(), failure.detail.c_str());
    connectionRequested = false;
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested || !result.advertisesService(TARGET_SERVICE_UUID))
    {
      return;
    }
    bluetooth.scanner().stop();
    connectionRequested = bluetooth.connect(result);
  });

  bluetooth.scanner().start(EspBleScanConfig());
}

void loop()
{
  bluetooth.update();
  delay(1);
}
