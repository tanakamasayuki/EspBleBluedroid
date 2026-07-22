#include <EspBleBluedroid.h>

static constexpr const char *TARGET_SERVICE_UUID = "180f";

EspBleBluedroid bluetooth;
bool connectionRequested = false;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Central";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE init failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    Serial.printf("Connected: id=%lu peer=%s mtu=%u\n",
      static_cast<unsigned long>(connection.id),
      connection.peerAddress.c_str(), connection.mtu);
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf("Disconnected: id=%lu\n",
      static_cast<unsigned long>(connection.id));
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
    if (!connectionRequested)
    {
      Serial.printf("Connect request rejected: %s (%s)\n",
        bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    }
  });

  EspBleScanConfig scanConfig;
  scanConfig.active = true;
  bluetooth.scanner().start(scanConfig);
}

void loop()
{
  bluetooth.update();
  delay(1);
}
