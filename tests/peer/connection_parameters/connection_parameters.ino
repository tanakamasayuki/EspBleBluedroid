#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *SERVICE_UUID = "1815";

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
EspBleConnectionId connectionId = 0;
bool connectionRequested = false;

static const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();
  if (!bluetooth.begin())
  {
    Serial.printf("INIT_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  const bool badRange = bluetooth.updateConnectionParameters(
    1, 81, 80, 0, 200);
  Serial.printf("BAD_RANGE_REJECTED %u error=%s\n",
    badRange ? 0 : 1, bluetooth.lastErrorName());
  const bool unknown = bluetooth.updateConnectionParameters(
    1, 80, 80, 0, 200);
  Serial.printf("UNKNOWN_ID_REJECTED %u error=%s\n",
    unknown ? 0 : 1, bluetooth.lastErrorName());

  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    Serial.printf(
      "CONNECTED id=%lu interval=%u latency=%u timeout=%u context=%s\n",
      static_cast<unsigned long>(connection.id),
      connection.connectionInterval,
      connection.peripheralLatency,
      connection.supervisionTimeout,
      contextName());
  });
  bluetooth.onConnectionParametersUpdated(
    [](const EspBleConnection &connection) {
      EspBleConnection snapshot;
      const bool stable =
        bluetooth.connection(connection.id, snapshot) &&
        snapshot.connectionInterval == connection.connectionInterval &&
        snapshot.peripheralLatency == connection.peripheralLatency &&
        snapshot.supervisionTimeout == connection.supervisionTimeout;
      Serial.printf(
        "PARAMS_UPDATED interval=%u latency=%u timeout=%u stable=%u context=%s\n",
        connection.connectionInterval,
        connection.peripheralLatency,
        connection.supervisionTimeout,
        stable ? 1 : 0,
        contextName());
    });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    connectionId = 0;
    connectionRequested = false;
    Serial.printf("DISCONNECTED id=%lu context=%s\n",
      static_cast<unsigned long>(connection.id), contextName());
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested || !result.advertisesService(SERVICE_UUID)) return;
    bluetooth.scanner().stop();
    connectionRequested = bluetooth.connect(result);
    Serial.println(connectionRequested
      ? "CONNECT_REQUESTED" : "CONNECT_REQUEST_FAILED");
  });
  Serial.println("CENTRAL_READY");
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 's' && !connectionRequested)
    {
      Serial.println(
        bluetooth.scanner().start() ? "SCAN_STARTED" : "SCAN_START_FAILED");
    }
    else if (command == 'p' && connectionId != 0)
    {
      const bool accepted = bluetooth.updateConnectionParameters(
        connectionId, 80, 80, 0, 200);
      Serial.println(
        accepted ? "UPDATE_REQUESTED" : "UPDATE_REQUEST_FAILED");
    }
    else if (command == 'd' && connectionId != 0)
    {
      Serial.println(bluetooth.disconnect(connectionId)
        ? "DISCONNECT_REQUESTED" : "DISCONNECT_REQUEST_FAILED");
    }
  }
  bluetooth.update();
  delay(1);
}
