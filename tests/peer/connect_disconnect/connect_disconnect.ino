#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID =
  "8d47a640-8d3a-4d65-a76f-6f626c756564";

EspBleBluedroid bluetooth;
EspBleScanResult target;
bool updatesEnabled = true;
bool requestInFlight = false;
bool lifecycleComplete = false;
bool reinitialized = false;
uint8_t connectedSequence = 0;
EspBleConnectionId firstConnectionId = 0;
uint8_t failureCallbackCount = 0;

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
  const bool invalidAccepted = bluetooth.connect(
    "not-a-ble-address", EspBleAddressType::Public, 1000);
  Serial.printf("INVALID_ADDRESS_REJECTED %u error=%s\n",
    !invalidAccepted ? 1 : 0, bluetooth.lastErrorName());

  bluetooth.onConnected([](const EspBleConnection &connection) {
    ++connectedSequence;
    EspBleConnection snapshot;
    const bool stable = connection.id != 0 &&
      bluetooth.connectionCount() == 1 &&
      bluetooth.connection(connection.id, snapshot) &&
      snapshot.id == connection.id &&
      snapshot.peerAddress.equalsIgnoreCase(connection.peerAddress) &&
      connection.localRole == EspBleRole::Central;
    const bool freshId = connectedSequence == 1 ||
      connection.id != firstConnectionId;
    if (connectedSequence == 1) firstConnectionId = connection.id;
    Serial.printf("CONNECTED seq=%u id=%lu stable=%u fresh=%u mtu=%u\n",
      connectedSequence, static_cast<unsigned long>(connection.id), stable ? 1 : 0,
      freshId ? 1 : 0,
      connection.mtu);
    Serial.printf("DISCONNECT_ACCEPTED %u\n",
      bluetooth.disconnect(connection.id) ? 1 : 0);
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf("DISCONNECTED seq=%u id=%lu count=%u\n",
      connectedSequence,
      static_cast<unsigned long>(connection.id),
      static_cast<unsigned>(bluetooth.connectionCount()));
    requestInFlight = false;
    if (connectedSequence == 1)
    {
      Serial.println("RECONNECT_COMMAND_READY");
    }
    else
    {
      requestInFlight = bluetooth.connect(
        "02:00:00:00:00:01", EspBleAddressType::Public, 1200);
      Serial.printf("FAILURE_REQUEST_ACCEPTED %u\n",
        requestInFlight ? 1 : 0);
    }
  });
  bluetooth.onConnectionFailed([](const EspBleConnectionFailure &failure) {
    ++failureCallbackCount;
    Serial.printf("CONNECTION_FAILED address=%s error=%u detail=%s\n",
      failure.peerAddress.c_str(), static_cast<unsigned>(failure.error),
      failure.detail.c_str());
    requestInFlight = false;
    lifecycleComplete = true;
  });

  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (requestInFlight || !result.advertisesService(SERVICE_UUID))
    {
      return;
    }
    bluetooth.scanner().stop();
    target = result;
    const bool accepted = bluetooth.connect(target, 5000);
    requestInFlight = accepted;
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
    else if (command == 'r')
    {
      EspBleScanConfig config;
      config.active = true;
      config.wantDuplicates = true;
      config.intervalMilliseconds = 100;
      config.windowMilliseconds = 50;
      Serial.printf("RECONNECT_SCAN_STARTED %u\n",
        bluetooth.scanner().start(config) ? 1 : 0);
    }
    else if (command == 'e')
    {
      if (!bluetooth.initialized())
      {
        bluetooth.begin(makeConfig());
      }
      const uint8_t failuresBeforeEnd = failureCallbackCount;
      const bool accepted = bluetooth.connect(
        "02:00:00:00:00:02", EspBleAddressType::Public, 10000);
      const uint32_t startedAt = millis();
      bluetooth.end();
      const uint32_t elapsed = millis() - startedAt;
      Serial.printf("END_DURING_CONNECT accepted=%u elapsed=%lu initialized=%u\n",
        accepted ? 1 : 0, static_cast<unsigned long>(elapsed),
        bluetooth.initialized() ? 1 : 0);
      const bool beganAgain = bluetooth.begin(makeConfig());
      bluetooth.update();
      Serial.printf("END_REINITIALIZED %u stale_failures=%u\n",
        beganAgain ? 1 : 0,
        static_cast<unsigned>(failureCallbackCount - failuresBeforeEnd));
    }
  }

  if (updatesEnabled)
  {
    bluetooth.update();
  }
  if (lifecycleComplete && !reinitialized)
  {
    reinitialized = true;
    bluetooth.end();
    const bool beganAgain = bluetooth.begin(makeConfig());
    Serial.printf("REINITIALIZED %u\n", beganAgain ? 1 : 0);
    bluetooth.end();
  }
  delay(1);
}
