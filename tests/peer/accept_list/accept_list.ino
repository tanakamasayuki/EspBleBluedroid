#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID = "fead";
static constexpr uint32_t CONNECT_TIMEOUT_MS = 4000;

EspBleBluedroid bluetooth;
bool connectRequested = false;
EspBleConnectionId connectionId = 0;
enum class ScanMode
{
  Connect,
  Observe,
};
ScanMode scanMode = ScanMode::Connect;
bool targetSeen = false;
String targetAddress;
EspBleAddressType targetAddressType = EspBleAddressType::Public;

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
    if (!result.advertisesService(SERVICE_UUID)) return;
    if (scanMode == ScanMode::Observe)
    {
      targetSeen = true;
      targetAddress = result.address;
      targetAddressType = result.addressType;
      return;
    }
    if (connectRequested) return;
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
      scanMode = ScanMode::Connect;
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
    else if (command == 's' || command == 'f')
    {
      scanMode = ScanMode::Observe;
      targetSeen = false;
      EspBleScanConfig config;
      config.active = true;
      config.acceptListOnly = command == 'f';
      Serial.println(bluetooth.scanner().start(config)
        ? "OBSERVE_STARTED" : "OBSERVE_START_FAILED");
    }
    else if (command == 'n')
    {
      bluetooth.scanner().stop();
      Serial.printf("OBSERVED target=%u address=%s\n",
        targetSeen ? 1 : 0, targetAddress.c_str());
    }
    else if (command == 'a')
    {
      const bool added = targetAddress.length() > 0 &&
        bluetooth.addToAcceptList(
          targetAddress.c_str(), targetAddressType);
      Serial.printf("CENTRAL_ACCEPT_LIST added=%u count=%u\n",
        added ? 1 : 0,
        static_cast<unsigned>(bluetooth.acceptListCount()));
    }
    else if (command == 'x')
    {
      bluetooth.clearAcceptList();
      Serial.printf("CENTRAL_ACCEPT_LIST added=0 count=%u\n",
        static_cast<unsigned>(bluetooth.acceptListCount()));
    }
  }
  bluetooth.update();
  delay(1);
}
