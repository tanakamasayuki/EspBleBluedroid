// Cross-stack duplicate UUIDs: this library's GATT client against an EspBle
// (NimBLE) peripheral that publishes two Characteristics sharing one UUID inside
// a single Service. This is the `peer_device` half — the board running the
// library under test — while the ESP32-S3 running EspBle is the parent fixture.
//
// The spec allows the duplicates and this library's *server* API rejects them,
// because it addresses local attributes by UUID and two attributes with one name
// cannot be told apart (`peer/duplicate_uuid` pins that rejection and its error
// string). The *client* half has no such excuse: a peer may publish them, so the
// handle-addressed operations must select one exact attribute out of the
// discovery snapshot. `peer/duplicate_uuid` verifies that against the bundled
// Arduino Bluedroid wrapper; here the responder is NimBLE, so the routing is
// checked against a second implementation instead of the stack this library sits
// on.
//
// The local rejection is reported here too, so this file records both halves of
// the asymmetry in one place: what this library refuses to publish is exactly
// what it must still be able to consume.
//
// Every step is driven by a serial command, so a failure names the step.

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Interop UUIDs live in the 01xx suite-tag range (tests/TEST_PLAN.md).
static constexpr const char *SERVICE_UUID =
  "01030000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *CHARACTERISTIC_UUID =
  "01030001-b1dd-4d00-9e5a-627564726f69";
// A local service, never advertised, used only to record the server-side
// restriction next to the client behaviour it forces.
static constexpr const char *LOCAL_SERVICE_UUID =
  "01030010-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *LOCAL_CHARACTERISTIC_UUID =
  "01030011-b1dd-4d00-9e5a-627564726f69";

static constexpr uint8_t WRITE_VALUE[] = {0x44, 0x00, 0xf4};

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
bool connectionRequested = false;
EspBleConnectionId connectionId = 0;
uint16_t firstHandle = 0;
uint16_t secondHandle = 0;

// Captured while registering and reported on request: the first serial output
// after flashing can be lost while the port reopens, and a contract assertion
// must not depend on that.
struct RegistrationReport
{
  bool baseAccepted = false;
  bool duplicateRejected = false;
  String error;
  String detail;
} registration;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

const char *errorName(EspBleError error)
{
  switch (error)
  {
    case EspBleError::None: return "NONE";
    case EspBleError::InvalidState: return "INVALID_STATE";
    case EspBleError::InvalidArgument: return "INVALID_ARGUMENT";
    case EspBleError::BackendFailure: return "BACKEND_FAILURE";
    case EspBleError::NotFound: return "NOT_FOUND";
    case EspBleError::Timeout: return "TIMEOUT";
    default: return "OTHER";
  }
}

void printHex(const String &value)
{
  for (size_t index = 0; index < value.length(); ++index)
  {
    Serial.printf("%02x", static_cast<uint8_t>(value[index]));
  }
}

void registerLocalServer()
{
  auto &server = bluetooth.gattServer();
  EspBleGattCharacteristicConfig characteristicConfig;
  characteristicConfig.readable = true;

  const EspBleGattService service = server.addService(LOCAL_SERVICE_UUID);
  const EspBleGattCharacteristic characteristic = server.addCharacteristic(
    service, LOCAL_CHARACTERISTIC_UUID, characteristicConfig);
  registration.baseAccepted = service.valid() && characteristic.valid();

  const EspBleGattCharacteristic duplicate = server.addCharacteristic(
    service, LOCAL_CHARACTERISTIC_UUID, characteristicConfig);
  registration.duplicateRejected = !duplicate.valid();
  registration.error = bluetooth.lastErrorName();
  registration.detail = bluetooth.lastErrorDetail();
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();

  registerLocalServer();

  if (!bluetooth.begin())
  {
    Serial.printf("INIT_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    Serial.printf("CONNECTED id=%u\n", static_cast<unsigned>(connection.id));
  });
  bluetooth.onServicesDiscovered([](const EspBleGattResult &result) {
    size_t matches = 0;
    firstHandle = 0;
    secondHandle = 0;
    for (size_t index = 0;
         index < bluetooth.discoveredCharacteristicCount(result.connectionId);
         ++index)
    {
      EspBleGattCharacteristicInfo info;
      if (!bluetooth.discoveredCharacteristic(result.connectionId, index, info))
        continue;
      if (!info.serviceUuid.equalsIgnoreCase(SERVICE_UUID) ||
          !info.characteristicUuid.equalsIgnoreCase(CHARACTERISTIC_UUID))
        continue;
      if (matches == 0) firstHandle = info.handle;
      else if (matches == 1) secondHandle = info.handle;
      ++matches;
    }
    Serial.printf(
      "DISCOVERY success=%u matches=%u first=%u second=%u distinct=%u context=%s\n",
      result.success ? 1 : 0, static_cast<unsigned>(matches),
      static_cast<unsigned>(firstHandle), static_cast<unsigned>(secondHandle),
      firstHandle != 0 && secondHandle != 0 && firstHandle != secondHandle ? 1 : 0,
      contextName());
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    // The handle in the completion is what makes the answer attributable: with a
    // shared UUID, the value alone cannot say which attribute answered.
    Serial.printf("READ success=%u error=%s handle=%u which=%u hex=",
      result.success ? 1 : 0, errorName(result.error),
      static_cast<unsigned>(result.handle),
      result.handle == firstHandle
        ? 1 : (result.handle == secondHandle ? 2 : 0));
    printHex(result.value);
    Serial.printf(" context=%s\n", contextName());
  });
  bluetooth.onCharacteristicWritten([](const EspBleGattResult &result) {
    Serial.printf("WRITE success=%u error=%s handle=%u which=%u context=%s\n",
      result.success ? 1 : 0, errorName(result.error),
      static_cast<unsigned>(result.handle),
      result.handle == firstHandle
        ? 1 : (result.handle == secondHandle ? 2 : 0),
      contextName());
  });
  bluetooth.onSubscribed([](const EspBleGattResult &result) {
    Serial.printf("SUBSCRIBED success=%u handle=%u which=%u context=%s\n",
      result.success ? 1 : 0, static_cast<unsigned>(result.handle),
      result.handle == firstHandle
        ? 1 : (result.handle == secondHandle ? 2 : 0),
      contextName());
  });
  bluetooth.onNotification([](const EspBleGattNotification &notification) {
    // Routing is the point: the value has to arrive tagged with the handle that
    // sent it, not with the first characteristic that happens to share the UUID.
    Serial.printf("NOTIFICATION handle=%u which=%u hex=",
      static_cast<unsigned>(notification.handle),
      notification.handle == firstHandle
        ? 1 : (notification.handle == secondHandle ? 2 : 0));
    printHex(notification.value);
    Serial.printf(" context=%s\n", contextName());
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf("DISCONNECTED id=%u dropped=%u context=%s\n",
      static_cast<unsigned>(connection.id),
      static_cast<unsigned>(bluetooth.droppedEventCount()), contextName());
  });
  bluetooth.onConnectionFailed([](const EspBleConnectionFailure &failure) {
    Serial.printf("CONNECT_FAILED detail=%s\n", failure.detail.c_str());
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested || !result.advertisesService(SERVICE_UUID)) return;
    connectionRequested = true;
    bluetooth.scanner().stop();
    Serial.printf("TARGET_FOUND address=%s name=%s\n",
      result.address.c_str(), result.name.c_str());
    Serial.printf("CONNECT_REQUESTED %u\n",
      bluetooth.connect(result, 10000) ? 1 : 0);
  });

  Serial.println("INTEROP_DUPLICATE_UUID_READY");
}

void loop()
{
  bluetooth.update();
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == 'r')
    {
      Serial.printf("LOCAL_BASE_ACCEPTED %u\n", registration.baseAccepted ? 1 : 0);
      Serial.printf("LOCAL_DUPLICATE_REJECTED %u error=%s detail=%s\n",
        registration.duplicateRejected ? 1 : 0, registration.error.c_str(),
        registration.detail.c_str());
    }
    else if (command == 'c')
    {
      EspBleScanConfig scanConfig;
      scanConfig.active = true;
      Serial.printf("SCAN_STARTED %u\n",
        bluetooth.scanner().start(scanConfig) ? 1 : 0);
    }
    else if (command == 'd')
    {
      Serial.printf("DISCOVERY_REQUESTED %u\n",
        bluetooth.discoverServices(connectionId, 5000) ? 1 : 0);
    }
    else if (command == 'u')
    {
      // The UUID form can only ever reach one of the two. Which one it reaches is
      // part of the contract, so it is read rather than avoided.
      Serial.printf("UUID_READ_REQUESTED %u\n",
        bluetooth.readCharacteristic(
          connectionId, SERVICE_UUID, CHARACTERISTIC_UUID, 5000) ? 1 : 0);
    }
    else if (command == '1')
    {
      Serial.printf("FIRST_READ_REQUESTED %u\n",
        bluetooth.readCharacteristic(connectionId, firstHandle, 5000) ? 1 : 0);
    }
    else if (command == '2')
    {
      Serial.printf("SECOND_READ_REQUESTED %u\n",
        bluetooth.readCharacteristic(connectionId, secondHandle, 5000) ? 1 : 0);
    }
    else if (command == 'w')
    {
      Serial.printf("SECOND_WRITE_REQUESTED %u\n",
        bluetooth.writeCharacteristic(connectionId, secondHandle, WRITE_VALUE,
          sizeof(WRITE_VALUE), true, 5000) ? 1 : 0);
    }
    else if (command == 's')
    {
      Serial.printf("SECOND_SUBSCRIBE_REQUESTED %u\n",
        bluetooth.subscribe(connectionId, secondHandle, true, 5000) ? 1 : 0);
    }
    else if (command == 'x')
    {
      Serial.printf("DISCONNECT_REQUESTED %u\n",
        bluetooth.disconnect(connectionId) ? 1 : 0);
    }
  }
  delay(1);
}
