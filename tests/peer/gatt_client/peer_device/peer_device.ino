#include <BLEDevice.h>
#include <BLE2902.h>
#include <BLEServer.h>

static constexpr const char *SERVICE_UUID =
  "8d47a650-8d3a-4d65-a76f-6f626c756564";
static constexpr const char *CHARACTERISTIC_UUID =
  "8d47a651-8d3a-4d65-a76f-6f626c756564";
static constexpr const char *DESCRIPTOR_UUID =
  "8d47a652-8d3a-4d65-a76f-6f626c756564";

class CharacteristicCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *characteristic) override
  {
    const String value = characteristic->getValue();
    Serial.printf("GATT_PEER_WRITE length=%u hex=",
      static_cast<unsigned>(value.length()));
    for (size_t index = 0; index < value.length(); ++index)
    {
      Serial.printf("%02x", static_cast<uint8_t>(value[index]));
    }
    Serial.println();
  }
};

class ServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *) override
  {
    Serial.println("GATT_PEER_CONNECTED");
  }
};

class DescriptorCallbacks : public BLEDescriptorCallbacks
{
  void onWrite(BLEDescriptor *descriptor) override
  {
    const uint8_t *value = descriptor->getValue();
    Serial.printf("GATT_PEER_DESCRIPTOR_WRITE length=%u hex=",
      static_cast<unsigned>(descriptor->getLength()));
    for (size_t index = 0; index < descriptor->getLength(); ++index)
    {
      Serial.printf("%02x", value[index]);
    }
    Serial.println();
  }
};

ServerCallbacks callbacks;
CharacteristicCallbacks characteristicCallbacks;
DescriptorCallbacks descriptorCallbacks;

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid GATT Peer");
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(&callbacks);
  BLEService *service = server->createService(SERVICE_UUID);
  BLECharacteristic *characteristic = service->createCharacteristic(
    CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_WRITE_NR |
      BLECharacteristic::PROPERTY_NOTIFY);
  characteristic->setCallbacks(&characteristicCallbacks);
  characteristic->addDescriptor(new BLE2902());
  BLEDescriptor *descriptor = new BLEDescriptor(DESCRIPTOR_UUID);
  const uint8_t descriptorValue[] = {0x44, 0x00, 0xff, 0x53};
  descriptor->setValue(descriptorValue, sizeof(descriptorValue));
  descriptor->setCallbacks(&descriptorCallbacks);
  characteristic->addDescriptor(descriptor);
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
  if (Serial.available() && Serial.read() == 'n')
  {
    BLEServer *server = BLEDevice::getServer();
    BLEService *service = server->getServiceByUUID(SERVICE_UUID);
    BLECharacteristic *characteristic =
      service->getCharacteristic(CHARACTERISTIC_UUID);
    const uint8_t value[] = {0x4e, 0x00, 0xff, 0x59};
    characteristic->setValue(value, sizeof(value));
    characteristic->notify();
    Serial.println("GATT_PEER_NOTIFIED");
  }
  delay(1);
}
