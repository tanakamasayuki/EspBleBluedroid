// Pins both halves of the duplicate-UUID contract.
//
// Server: this library refuses a second Characteristic with the same UUID inside
// one Service, and a second Descriptor with the same UUID under one
// Characteristic. The spec allows both, and the Arduino Bluedroid wrapper can
// create them (its own BLEHIDDevice makes several 0x2a4d Report characteristics),
// so this is a restriction of *this library*, not of Bluedroid: the public API
// addresses server attributes by UUID, and two attributes with one name cannot
// be told apart. The test exists so that lifting the restriction is a deliberate
// change with a test to invert — it is a prerequisite for HID over GATT.
// The same UUID in a *different* Service stays legal and is verified here too.
//
// Client: a peer may legally publish duplicates, so the client half must be able
// to work with them. The handle-addressed operations select one exact
// characteristic out of the discovery snapshot, which is the only way to reach
// the second one.

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Local server attributes, used only for the registration contract.
static constexpr const char *LOCAL_SERVICE_UUID =
  "00030000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *LOCAL_CHARACTERISTIC_UUID =
  "00030001-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *LOCAL_DESCRIPTOR_UUID =
  "00030002-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *LOCAL_SECOND_SERVICE_UUID =
  "00030010-b1dd-4d00-9e5a-627564726f69";

// A UUID whose last group has 14 hex digits instead of 12. It looks right at a
// glance, which is exactly why registration has to reject it: the Arduino
// wrapper leaves BLEUUID unset for a malformed string and then copies from its
// null native pointer while begin() builds the database, crashing at a point far
// from the call that caused it.
static constexpr const char *MALFORMED_UUID =
  "00030099-b1dd-4d00-9e5a-627564726f6964";

// The peer's service, which carries two characteristics with one UUID.
static constexpr const char *PEER_SERVICE_UUID =
  "00030020-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *PEER_CHARACTERISTIC_UUID =
  "00030021-b1dd-4d00-9e5a-627564726f69";

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
bool connectionRequested = false;
bool scanning = false;

// Results of the registration contract, captured while registering and reported
// on request. They are not printed from setup() because the first serial output
// after flashing can be lost while the port reopens, and a contract assertion
// must not depend on that.
struct RegistrationReport
{
  bool baseAccepted = false;
  bool duplicateCharacteristicRejected = false;
  String characteristicError;
  String characteristicDetail;
  bool duplicateDescriptorRejected = false;
  String descriptorError;
  String descriptorDetail;
  bool sameUuidOtherServiceAccepted = false;
  bool malformedServiceRejected = false;
  String malformedServiceError;
  String malformedServiceDetail;
  bool malformedCharacteristicRejected = false;
} registration;
uint16_t firstHandle = 0;
uint16_t secondHandle = 0;
uint8_t readPhase = 0;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void registerLocalServer()
{
  auto &server = bluetooth.gattServer();
  EspBleGattCharacteristicConfig characteristicConfig;
  characteristicConfig.readable = true;
  EspBleGattDescriptorConfig descriptorConfig;

  const EspBleGattService service = server.addService(LOCAL_SERVICE_UUID);
  const EspBleGattCharacteristic characteristic =
    server.addCharacteristic(service, LOCAL_CHARACTERISTIC_UUID, characteristicConfig);
  registration.baseAccepted = service.valid() && characteristic.valid();

  const EspBleGattCharacteristic duplicate =
    server.addCharacteristic(service, LOCAL_CHARACTERISTIC_UUID, characteristicConfig);
  registration.duplicateCharacteristicRejected = !duplicate.valid();
  registration.characteristicError = bluetooth.lastErrorName();
  registration.characteristicDetail = bluetooth.lastErrorDetail();

  const EspBleGattDescriptor descriptor =
    server.addDescriptor(characteristic, LOCAL_DESCRIPTOR_UUID, descriptorConfig);
  const EspBleGattDescriptor duplicateDescriptor =
    server.addDescriptor(characteristic, LOCAL_DESCRIPTOR_UUID, descriptorConfig);
  registration.duplicateDescriptorRejected =
    descriptor.valid() && !duplicateDescriptor.valid();
  registration.descriptorError = bluetooth.lastErrorName();
  registration.descriptorDetail = bluetooth.lastErrorDetail();

  // The restriction is per Service: the same UUID in another Service is legal
  // here as well as in the spec.
  const EspBleGattService secondService =
    server.addService(LOCAL_SECOND_SERVICE_UUID);
  const EspBleGattCharacteristic sameUuidElsewhere = server.addCharacteristic(
    secondService, LOCAL_CHARACTERISTIC_UUID, characteristicConfig);
  registration.sameUuidOtherServiceAccepted = sameUuidElsewhere.valid();

  const EspBleGattService malformedService = server.addService(MALFORMED_UUID);
  registration.malformedServiceRejected = !malformedService.valid();
  registration.malformedServiceError = bluetooth.lastErrorName();
  registration.malformedServiceDetail = bluetooth.lastErrorDetail();
  const EspBleGattCharacteristic malformedCharacteristic =
    server.addCharacteristic(service, MALFORMED_UUID, characteristicConfig);
  registration.malformedCharacteristicRejected = !malformedCharacteristic.valid();
}

void reportRegistration()
{
  Serial.printf("LOCAL_BASE_ACCEPTED %u\n", registration.baseAccepted ? 1 : 0);
  Serial.printf("DUPLICATE_CHARACTERISTIC_REJECTED %u error=%s detail=%s\n",
    registration.duplicateCharacteristicRejected ? 1 : 0,
    registration.characteristicError.c_str(),
    registration.characteristicDetail.c_str());
  Serial.printf("DUPLICATE_DESCRIPTOR_REJECTED %u error=%s detail=%s\n",
    registration.duplicateDescriptorRejected ? 1 : 0,
    registration.descriptorError.c_str(),
    registration.descriptorDetail.c_str());
  Serial.printf("SAME_UUID_OTHER_SERVICE_ACCEPTED %u\n",
    registration.sameUuidOtherServiceAccepted ? 1 : 0);
  Serial.printf("MALFORMED_UUID_REJECTED service=%u characteristic=%u error=%s detail=%s\n",
    registration.malformedServiceRejected ? 1 : 0,
    registration.malformedCharacteristicRejected ? 1 : 0,
    registration.malformedServiceError.c_str(),
    registration.malformedServiceDetail.c_str());
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
    Serial.printf("CONNECTED id=%u\n", static_cast<unsigned>(connection.id));
    const bool accepted = bluetooth.discoverServices(connection.id, 5000);
    Serial.printf("DISCOVERY_REQUESTED %u\n", accepted ? 1 : 0);
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
      if (!info.serviceUuid.equalsIgnoreCase(PEER_SERVICE_UUID) ||
          !info.characteristicUuid.equalsIgnoreCase(PEER_CHARACTERISTIC_UUID))
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
    // The UUID form can only ever reach one of the two, so the handle form is
    // what the rest of this scenario uses.
    const bool accepted =
      bluetooth.readCharacteristic(result.connectionId, firstHandle, 5000);
    Serial.printf("FIRST_READ_REQUESTED %u\n", accepted ? 1 : 0);
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    const uint8_t byte =
      result.value.length() > 0 ? static_cast<uint8_t>(result.value[0]) : 0;
    if (readPhase++ == 0)
    {
      Serial.printf("FIRST_READ success=%u handle=%u byte=%02x context=%s\n",
        result.success ? 1 : 0, static_cast<unsigned>(result.handle), byte,
        contextName());
      const bool accepted =
        bluetooth.readCharacteristic(result.connectionId, secondHandle, 5000);
      Serial.printf("SECOND_READ_REQUESTED %u\n", accepted ? 1 : 0);
      return;
    }
    Serial.printf("SECOND_READ success=%u handle=%u byte=%02x context=%s\n",
      result.success ? 1 : 0, static_cast<unsigned>(result.handle), byte,
      contextName());
    const bool accepted =
      bluetooth.subscribe(result.connectionId, secondHandle, true, 5000);
    Serial.printf("SUBSCRIBE_REQUESTED %u\n", accepted ? 1 : 0);
  });
  bluetooth.onSubscribed([](const EspBleGattResult &result) {
    Serial.printf("SUBSCRIBED success=%u handle=%u context=%s\n",
      result.success ? 1 : 0, static_cast<unsigned>(result.handle),
      contextName());
  });
  bluetooth.onNotification([](const EspBleGattNotification &notification) {
    // Routing is the point: the value has to arrive tagged with the handle that
    // sent it, not with the first characteristic that happens to share the UUID.
    Serial.printf(
      "NOTIFICATION handle=%u second=%u byte=%02x length=%u context=%s\n",
      static_cast<unsigned>(notification.handle),
      notification.handle == secondHandle ? 1 : 0,
      notification.value.length() > 0
        ? static_cast<uint8_t>(notification.value[0]) : 0,
      static_cast<unsigned>(notification.value.length()), contextName());
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf("DISCONNECTED id=%u context=%s\n",
      static_cast<unsigned>(connection.id), contextName());
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested || !result.advertisesService(PEER_SERVICE_UUID))
      return;
    connectionRequested = true;
    bluetooth.scanner().stop();
    Serial.printf("TARGET_FOUND %s\n", result.address.c_str());
    Serial.printf("CONNECT_REQUESTED %u\n",
      bluetooth.connect(result, 10000) ? 1 : 0);
  });

  Serial.println("DUPLICATE_UUID_READY");
}

void loop()
{
  bluetooth.update();
  if (Serial.available())
  {
    const int command = Serial.read();
    if (command == 'r')
    {
      reportRegistration();
    }
    else if (command == 'c' && !scanning)
    {
      scanning = true;
      EspBleScanConfig scanConfig;
      scanConfig.active = true;
      Serial.printf("SCAN_STARTED %u\n",
        bluetooth.scanner().start(scanConfig) ? 1 : 0);
    }
    else if (command == 'd')
    {
      Serial.printf("DISCONNECT_REQUESTED %u\n",
        bluetooth.disconnect(1) ? 1 : 0);
    }
  }
  delay(1);
}
