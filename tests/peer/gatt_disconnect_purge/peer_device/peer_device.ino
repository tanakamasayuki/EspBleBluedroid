// Raw Arduino-ESP32 Bluedroid peripheral with one readable Characteristic, kept
// advertising so the device under test can reconnect after tearing a link down
// mid-operation.

#include <BLEDevice.h>
#include <BLEServer.h>

static constexpr const char *SERVICE_UUID =
  "00010000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *CHARACTERISTIC_UUID =
  "00010001-b1dd-4d00-9e5a-627564726f69";

static constexpr uint8_t VALUE[] = {0x9c, 0x01, 0xfe};

class ServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *) override
  {
    Serial.println("PEER_CONNECTED");
  }

  void onDisconnect(BLEServer *) override
  {
    Serial.println("PEER_DISCONNECTED");
    // Advertise again so the second connection of the scenario can happen.
    BLEDevice::startAdvertising();
  }
};

ServerCallbacks callbacks;

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid Disconnect Purge Peer");
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(&callbacks);
  BLEService *service = server->createService(SERVICE_UUID);
  BLECharacteristic *characteristic = service->createCharacteristic(
    CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ);
  characteristic->setValue(const_cast<uint8_t *>(VALUE), sizeof(VALUE));
  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.printf("DISCONNECT_PURGE_PEER_READY length=%u\n",
    static_cast<unsigned>(sizeof(VALUE)));
}

void loop()
{
  delay(10);
}
