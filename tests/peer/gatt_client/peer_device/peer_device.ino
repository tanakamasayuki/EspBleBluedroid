#include <BLEDevice.h>
#include <BLEServer.h>

static constexpr const char *SERVICE_UUID =
  "8d47a650-8d3a-4d65-a76f-6f626c756564";
static constexpr const char *CHARACTERISTIC_UUID =
  "8d47a651-8d3a-4d65-a76f-6f626c756564";

class ServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *) override
  {
    Serial.println("GATT_PEER_CONNECTED");
  }
};

ServerCallbacks callbacks;

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid GATT Peer");
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(&callbacks);
  BLEService *service = server->createService(SERVICE_UUID);
  BLECharacteristic *characteristic = service->createCharacteristic(
    CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ);
  const uint8_t value[] = {0x00, 0x41, 0xff, 0x42};
  characteristic->setValue(value, sizeof(value));
  service->start();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("GATT_PEER_READY");
}

void loop()
{
  delay(1);
}
