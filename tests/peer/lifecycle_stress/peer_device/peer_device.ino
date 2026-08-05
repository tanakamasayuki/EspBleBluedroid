// The instrument for the lifecycle scenario: a raw Arduino-ESP32 peripheral that
// stays up for the whole run while the other board takes the stack down and brings
// it back eight times.
//
// Two things make it self-driving, so a round needs no serial round trip and the
// peer's own state cannot drift between rounds: it re-advertises after every
// disconnect, and it notifies back whatever is written to it.

#include <BLEDevice.h>
#include <BLE2902.h>
#include <BLEServer.h>

static constexpr const char *SERVICE_UUID =
  "00120000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *CHARACTERISTIC_UUID =
  "00120001-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *DEVICE_NAME = "Bluedroid Lifecycle Peer";

unsigned connects = 0;
unsigned disconnects = 0;
unsigned writes = 0;

class CharacteristicCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *characteristic) override
  {
    ++writes;
    // Notify the value straight back: this is what ends a round on the other side.
    characteristic->notify();
    Serial.printf("PEER_WRITE count=%u\n", writes);
  }
};

class ServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *) override
  {
    ++connects;
    Serial.printf("PEER_CONNECTED count=%u\n", connects);
  }
  void onDisconnect(BLEServer *) override
  {
    ++disconnects;
    Serial.printf("PEER_DISCONNECTED count=%u\n", disconnects);
    // Advertising stops on connect, so every round needs it started again.
    BLEDevice::startAdvertising();
  }
};

ServerCallbacks serverCallbacks;
CharacteristicCallbacks characteristicCallbacks;

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init(DEVICE_NAME);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(&serverCallbacks);
  BLEService *service = server->createService(SERVICE_UUID);
  BLECharacteristic *characteristic = service->createCharacteristic(
    CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_NOTIFY);
  characteristic->setCallbacks(&characteristicCallbacks);
  characteristic->addDescriptor(new BLE2902());
  const uint8_t value[] = {0x4c, 0x49, 0x46, 0x45};
  characteristic->setValue(value, sizeof(value));
  service->start();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("LIFECYCLE_PEER_READY");
}

void loop()
{
  if (Serial.available() && Serial.read() == '?')
  {
    Serial.printf("PEER_STATE connects=%u disconnects=%u writes=%u heap=%u\n",
      connects, disconnects, writes,
      static_cast<unsigned>(ESP.getFreeHeap()));
  }
  delay(1);
}
