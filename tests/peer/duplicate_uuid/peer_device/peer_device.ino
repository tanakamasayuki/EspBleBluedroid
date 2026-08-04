// Raw Arduino-ESP32 Bluedroid peripheral publishing two Characteristics with the
// same UUID inside one Service — which the specification allows and the bundled
// wrapper can create, as its own BLEHIDDevice does for HID Report
// characteristics. The public client under test has to tell them apart by
// attribute handle.

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>

static constexpr const char *SERVICE_UUID =
  "00030020-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *CHARACTERISTIC_UUID =
  "00030021-b1dd-4d00-9e5a-627564726f69";

// Distinct first bytes so a read proves which of the two answered.
static constexpr uint8_t FIRST_VALUE[] = {0xa1, 0x01};
static constexpr uint8_t SECOND_VALUE[] = {0xb2, 0x02};
static constexpr uint8_t SECOND_NOTIFICATION[] = {0xb2, 0x03};

BLECharacteristic *first;
BLECharacteristic *second;

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid Duplicate UUID Peer");
  BLEServer *server = BLEDevice::createServer();
  BLEService *service = server->createService(SERVICE_UUID);

  const uint32_t properties = BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY;
  first = service->createCharacteristic(CHARACTERISTIC_UUID, properties);
  first->addDescriptor(new BLE2902());
  first->setValue(const_cast<uint8_t *>(FIRST_VALUE), sizeof(FIRST_VALUE));
  second = service->createCharacteristic(CHARACTERISTIC_UUID, properties);
  second->addDescriptor(new BLE2902());
  second->setValue(const_cast<uint8_t *>(SECOND_VALUE), sizeof(SECOND_VALUE));
  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.printf("DUPLICATE_UUID_PEER_READY first=%u second=%u distinct=%u\n",
    first->getHandle(), second->getHandle(),
    first->getHandle() != second->getHandle() ? 1 : 0);
}

void loop()
{
  if (Serial.available() && Serial.read() == 'n')
  {
    // Only the second one notifies, so a value delivered against the first
    // handle would be a routing bug rather than a lucky match.
    second->setValue(
      const_cast<uint8_t *>(SECOND_NOTIFICATION), sizeof(SECOND_NOTIFICATION));
    second->notify();
    Serial.printf("PEER_NOTIFIED handle=%u\n", second->getHandle());
  }
  delay(1);
}
