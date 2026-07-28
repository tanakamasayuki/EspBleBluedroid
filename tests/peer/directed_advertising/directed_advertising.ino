#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;
bool connectRequested = false;
EspBleConnectionId connectionId = 0;
String expectedPeerAddress;

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
    if (connectRequested ||
        !result.address.equalsIgnoreCase(expectedPeerAddress))
    {
      return;
    }
    connectRequested = true;
    bluetooth.scanner().stop();
    Serial.printf(
      "DIRECTED_RESULT address=%s type=%u connectable=%u scannable=%u "
      "name=%u manufacturer=%u services=%u service_data=%u\n",
      result.address.c_str(),
      static_cast<unsigned>(result.addressType),
      result.connectable ? 1 : 0,
      result.scannable ? 1 : 0,
      result.hasName() ? 1 : 0,
      result.hasManufacturerData() ? 1 : 0,
      static_cast<unsigned>(result.serviceUuidCount),
      static_cast<unsigned>(result.serviceDataCount));
    if (!bluetooth.connect(result, 5000))
    {
      Serial.printf("CONNECT_REJECTED %s\n", bluetooth.lastErrorName());
    }
  });
  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    Serial.printf("CENTRAL_CONNECTED id=%lu mtu=%u\n",
      static_cast<unsigned long>(connection.id),
      static_cast<unsigned>(connection.mtu));
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    connectionId = 0;
    Serial.printf("CENTRAL_DISCONNECTED id=%lu\n",
      static_cast<unsigned long>(connection.id));
  });
  bluetooth.onConnectionFailed(
    [](const EspBleConnectionFailure &failure) {
      Serial.printf("CENTRAL_CONNECT_FAILED error=%u\n",
        static_cast<unsigned>(failure.error));
    });

  Serial.printf("CENTRAL_READY address=%s\n",
    bluetooth.localAddress().c_str());
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'p')
    {
      expectedPeerAddress = Serial.readStringUntil('\n');
      expectedPeerAddress.trim();
      Serial.printf("PEER_SET address=%s\n", expectedPeerAddress.c_str());
    }
    else if (command == 's')
    {
      connectRequested = false;
      EspBleScanConfig config;
      config.active = false;
      config.durationSeconds = 5;
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
