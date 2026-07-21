#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#if !defined(CONFIG_BLUEDROID_ENABLED) || defined(CONFIG_NIMBLE_ENABLED)
#error "EspBleBluedroid peer tests require the Bluedroid backend"
#endif

static constexpr const char *SERVICE_UUID =
  "8d47a620-8d3a-4d65-a76f-6f626c756564";
static constexpr const char *CHARACTERISTIC_UUID =
  "8d47a621-8d3a-4d65-a76f-6f626c756564";

static BLECharacteristic *testCharacteristic = nullptr;
static volatile bool notificationPending = false;

class ServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *) override
  {
    Serial.println("PEER_CONNECTED");
  }
};

class CharacteristicCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *characteristic) override
  {
    String value = characteristic->getValue();
    Serial.printf("WRITE %s\n", value.c_str());
    notificationPending = true;
  }
};

void setup()
{
  Serial.begin(115200);
  delay(500);

  if (!BLEDevice::init("EspBleBluedroid Test Peer"))
  {
    Serial.println("PERIPHERAL_INIT_FAILED");
    return;
  }

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService *service = server->createService(SERVICE_UUID);
  testCharacteristic = service->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_NOTIFY);
  testCharacteristic->setCallbacks(new CharacteristicCallbacks());
  testCharacteristic->addDescriptor(new BLE2902());
  testCharacteristic->setValue("peer-ready");
  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("PERIPHERAL_READY");
}

void loop()
{
  if (notificationPending)
  {
    notificationPending = false;
    testCharacteristic->setValue("peer-notify");
    testCharacteristic->notify();
    Serial.println("NOTIFY_SENT");
  }
  delay(10);
}

