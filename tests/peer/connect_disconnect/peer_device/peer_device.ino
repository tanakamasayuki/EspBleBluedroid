#include <BLEDevice.h>
#include <BLEServer.h>

static constexpr const char *SERVICE_UUID =
  "8d47a640-8d3a-4d65-a76f-6f626c756564";

class Callbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *) override
  {
    Serial.println("PEER_CONNECTED");
  }

  void onDisconnect(BLEServer *) override
  {
    Serial.println("PEER_DISCONNECTED");
  }
};

Callbacks callbacks;

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid Connection Peer");
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(&callbacks);
  server->createService(SERVICE_UUID)->start();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("PEER_READY");
}

void loop()
{
  delay(1);
}
