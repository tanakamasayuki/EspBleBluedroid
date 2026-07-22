#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID =
  "8d47a640-8d3a-4d65-a76f-6f626c756564";

EspBleBluedroid bluetooth;
EspBleScanResult target;
bool updatesEnabled = true;
bool disconnected = false;
bool reinitialized = false;

EspBleConfig makeConfig()
{
  EspBleConfig config;
  config.deviceName = "Bluedroid Connection Central";
  return config;
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  if (!bluetooth.begin(makeConfig()))
  {
    Serial.printf("BEGIN_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    EspBleConnection snapshot;
    const bool stable = connection.id != 0 &&
      bluetooth.connectionCount() == 1 &&
      bluetooth.connection(connection.id, snapshot) &&
      snapshot.id == connection.id &&
      snapshot.peerAddress.equalsIgnoreCase(connection.peerAddress) &&
      connection.localRole == EspBleRole::Central;
    Serial.printf("CONNECTED id=%lu stable=%u mtu=%u\n",
      static_cast<unsigned long>(connection.id), stable ? 1 : 0,
      connection.mtu);
    Serial.printf("DISCONNECT_ACCEPTED %u\n",
      bluetooth.disconnect(connection.id) ? 1 : 0);
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf("DISCONNECTED id=%lu count=%u\n",
      static_cast<unsigned long>(connection.id),
      static_cast<unsigned>(bluetooth.connectionCount()));
    disconnected = true;
  });
  bluetooth.onConnectionFailed([](const EspBleConnectionFailure &failure) {
    Serial.printf("CONNECTION_FAILED %s %s\n",
      failure.peerAddress.c_str(), failure.detail.c_str());
  });

  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (!result.advertisesService(SERVICE_UUID))
    {
      return;
    }
    bluetooth.scanner().stop();
    target = result;
    const bool accepted = bluetooth.connect(target, 5000);
    Serial.printf("CONNECT_ACCEPTED %u\n", accepted ? 1 : 0);
    const bool concurrent = bluetooth.connect(target, 5000);
    Serial.printf("CONCURRENT_REJECTED %u error=%s\n",
      !concurrent ? 1 : 0, bluetooth.lastErrorName());
    updatesEnabled = false;
    Serial.println("UPDATE_PAUSED");
  });

  Serial.println("CENTRAL_READY");
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 's')
    {
      EspBleScanConfig config;
      config.active = true;
      config.intervalMilliseconds = 100;
      config.windowMilliseconds = 50;
      Serial.printf("SCAN_STARTED %u\n",
        bluetooth.scanner().start(config) ? 1 : 0);
    }
    else if (command == 'u')
    {
      Serial.println("UPDATE_START");
      updatesEnabled = true;
    }
  }

  if (updatesEnabled)
  {
    bluetooth.update();
  }
  if (disconnected && !reinitialized)
  {
    reinitialized = true;
    bluetooth.end();
    const bool beganAgain = bluetooth.begin(makeConfig());
    Serial.printf("REINITIALIZED %u\n", beganAgain ? 1 : 0);
    bluetooth.end();
  }
  delay(1);
}
