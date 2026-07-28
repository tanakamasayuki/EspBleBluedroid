#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID = "fead";
static constexpr uint32_t CONNECT_TIMEOUT_MS = 4000;

EspBleBluedroid bluetooth;
bool connectRequested = false;
EspBleConnectionId connectionId = 0;

void setup()
{
  Serial.begin(115200);
  delay(500);
  if (!bluetooth.begin())
  {
    Serial.printf("BEGIN_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectRequested || !result.advertisesService(SERVICE_UUID)) return;
    connectRequested = true;
    bluetooth.scanner().stop();
    Serial.printf("TARGET_FOUND %s\n", result.address.c_str());
    if (!bluetooth.connect(result, CONNECT_TIMEOUT_MS))
    {
      Serial.printf("CONNECT_REJECTED %s\n", bluetooth.lastErrorName());
    }
  });
  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    Serial.printf("CENTRAL_CONNECTED id=%lu\n",
      static_cast<unsigned long>(connection.id));
  });
  bluetooth.onConnectionFailed(
    [](const EspBleConnectionFailure &failure) {
      Serial.printf("CENTRAL_CONNECT_FAILED error=%u\n",
        static_cast<unsigned>(failure.error));
    });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    connectionId = 0;
    Serial.printf("CENTRAL_DISCONNECTED id=%lu\n",
      static_cast<unsigned long>(connection.id));
  });
  Serial.println("CENTRAL_READY");
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'c')
    {
      connectRequested = false;
      EspBleScanConfig config;
      config.active = true;
      Serial.println(bluetooth.scanner().start(config)
        ? "SCAN_STARTED" : "SCAN_START_FAILED");
    }
    else if (command == 'd' && connectionId != 0)
    {
      Serial.println(bluetooth.disconnect(connectionId)
        ? "DISCONNECT_REQUESTED" : "DISCONNECT_FAILED");
    }
  }
  bluetooth.update();
  delay(1);
}
