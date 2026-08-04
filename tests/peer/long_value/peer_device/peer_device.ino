// Raw Arduino-ESP32 Bluedroid peripheral publishing one Characteristic whose
// value is longer than any ATT response on this link, so the public client under
// test has something to truncate.

#include <BLEDevice.h>
#include <BLEServer.h>

static constexpr const char *SERVICE_UUID =
  "00040000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *CHARACTERISTIC_UUID =
  "00040001-b1dd-4d00-9e5a-627564726f69";

static constexpr size_t VALUE_LENGTH = 300;

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid Long Value Peer");
  BLEServer *server = BLEDevice::createServer();
  BLEService *service = server->createService(SERVICE_UUID);
  BLECharacteristic *characteristic = service->createCharacteristic(
    CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ);

  // A known ramp: byte i = i & 0xff. A truncated read must be its prefix.
  uint8_t value[VALUE_LENGTH];
  for (size_t index = 0; index < VALUE_LENGTH; ++index)
  {
    value[index] = static_cast<uint8_t>(index & 0xff);
  }
  characteristic->setValue(value, sizeof(value));
  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.printf("LONG_VALUE_PEER_READY length=%u\n",
    static_cast<unsigned>(VALUE_LENGTH));
}

void loop()
{
  delay(10);
}
