// What happens to a GATT operation that is already on the air when the link goes
// away.
//
// This library runs one operation at a time per connection on a dedicated worker
// task, and `disconnect()` is not deferred for it: the request goes to the
// backend immediately and the worker's blocking call fails. Two things must hold
// for an application not to hang:
//
// - the in-flight operation produces exactly one completion, whether success or
//   failure — an application that awaits a callback per request must never be
//   left waiting for one that will never come, and must never receive two;
// - the operation slot and the ATT resources are released, so the next
//   connection can discover and read normally.
//
// Both are silent when broken: the symptom is an application that stops making
// progress some time later, with no error anywhere. The scenario also checks
// `droppedEventCount()` so a completion that was produced but evicted from the
// event queue cannot be mistaken for one that was never produced.
//
// The complementary case — a second operation issued while one is in flight —
// is already covered by `peer/gatt_client` (CONCURRENT_GATT_REJECTED).

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *SERVICE_UUID =
  "00010000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *CHARACTERISTIC_UUID =
  "00010001-b1dd-4d00-9e5a-627564726f69";

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
bool connectionRequested = false;
uint16_t characteristicHandle = 0;
// Connection generation: 1 is the link that gets torn down mid-operation, 2 is
// the one that has to work afterwards.
uint8_t connectionGeneration = 0;
uint8_t readResults = 0;
bool disconnectDuringRead = false;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

const char *errorName(EspBleError error)
{
  switch (error)
  {
    case EspBleError::None: return "None";
    case EspBleError::InvalidState: return "InvalidState";
    case EspBleError::InvalidArgument: return "InvalidArgument";
    case EspBleError::BackendFailure: return "BackendFailure";
    case EspBleError::Timeout: return "Timeout";
    default: return "Other";
  }
}

void startScan()
{
  connectionRequested = false;
  EspBleScanConfig scanConfig;
  scanConfig.active = true;
  Serial.printf("SCAN_STARTED %u\n",
    bluetooth.scanner().start(scanConfig) ? 1 : 0);
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();

  if (!bluetooth.begin())
  {
    Serial.printf("INIT_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    ++connectionGeneration;
    Serial.printf("CONNECTED generation=%u id=%u\n",
      static_cast<unsigned>(connectionGeneration),
      static_cast<unsigned>(connection.id));
    const bool accepted = bluetooth.discoverServices(connection.id, 5000);
    Serial.printf("DISCOVERY_REQUESTED %u\n", accepted ? 1 : 0);
  });
  bluetooth.onServicesDiscovered([](const EspBleGattResult &result) {
    characteristicHandle = 0;
    for (size_t index = 0;
         index < bluetooth.discoveredCharacteristicCount(result.connectionId);
         ++index)
    {
      EspBleGattCharacteristicInfo info;
      if (bluetooth.discoveredCharacteristic(result.connectionId, index, info) &&
          info.serviceUuid.equalsIgnoreCase(SERVICE_UUID) &&
          info.characteristicUuid.equalsIgnoreCase(CHARACTERISTIC_UUID))
      {
        characteristicHandle = info.handle;
      }
    }
    Serial.printf("DISCOVERY generation=%u success=%u handle=%u context=%s\n",
      static_cast<unsigned>(connectionGeneration), result.success ? 1 : 0,
      static_cast<unsigned>(characteristicHandle), contextName());

    const bool accepted =
      bluetooth.readCharacteristic(result.connectionId, characteristicHandle, 5000);
    Serial.printf("READ_REQUESTED generation=%u %u\n",
      static_cast<unsigned>(connectionGeneration), accepted ? 1 : 0);

    if (connectionGeneration == 1 && accepted)
    {
      // Tear the link down while that read is in flight. The call is expected to
      // be accepted, not rejected: an application that has decided to disconnect
      // must not be told "still connected" because a read happens to be running.
      disconnectDuringRead = true;
      Serial.printf("DISCONNECT_DURING_READ_ACCEPTED %u\n",
        bluetooth.disconnect(result.connectionId) ? 1 : 0);
    }
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    // One line per completion, so the test can count them: two lines for one
    // request would mean a duplicate, none would mean a lost callback.
    ++readResults;
    Serial.printf(
      "READ_RESULT generation=%u count=%u success=%u error=%s length=%u detail=%s context=%s\n",
      static_cast<unsigned>(connectionGeneration),
      static_cast<unsigned>(readResults), result.success ? 1 : 0,
      errorName(result.error), static_cast<unsigned>(result.value.length()),
      result.detail.length() > 0 ? result.detail.c_str() : "none",
      contextName());
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf(
      "DISCONNECTED generation=%u id=%u during_read=%u results=%u dropped=%u context=%s\n",
      static_cast<unsigned>(connectionGeneration),
      static_cast<unsigned>(connection.id), disconnectDuringRead ? 1 : 0,
      static_cast<unsigned>(readResults),
      static_cast<unsigned>(bluetooth.droppedEventCount()), contextName());
  });
  bluetooth.onConnectionFailed([](const EspBleConnectionFailure &failure) {
    Serial.printf("CONNECT_FAILED error=%s\n",
      failure.detail.length() > 0 ? failure.detail.c_str() : "none");
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested || !result.advertisesService(SERVICE_UUID)) return;
    connectionRequested = true;
    bluetooth.scanner().stop();
    Serial.printf("TARGET_FOUND %s\n", result.address.c_str());
    Serial.printf("CONNECT_REQUESTED %u\n",
      bluetooth.connect(result, 10000) ? 1 : 0);
  });

  Serial.println("GATT_DISCONNECT_PURGE_READY");
}

void loop()
{
  bluetooth.update();
  if (Serial.available())
  {
    const int command = Serial.read();
    if (command == 'c')
    {
      startScan();
    }
    else if (command == 's')
    {
      // The state the next connection depends on: no operation left running.
      Serial.printf("STATE connections=%u dropped=%u results=%u\n",
        static_cast<unsigned>(bluetooth.connectionCount()),
        static_cast<unsigned>(bluetooth.droppedEventCount()),
        static_cast<unsigned>(readResults));
    }
  }
  delay(1);
}
