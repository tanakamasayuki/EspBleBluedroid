#include <BLEDevice.h>
#include <BLEServer.h>

static constexpr const char *SERVICE_UUID = "1815";

class Callbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *) override
  {
    Serial.println("PEER_CONNECTED");
  }

  void onDisconnect(BLEServer *) override
  {
    Serial.println("PEER_DISCONNECTED");
    BLEDevice::startAdvertising();
  }

  void onConnParamsUpdate(
    esp_bd_addr_t,
    uint16_t interval,
    uint16_t latency,
    uint16_t timeout,
    esp_bt_status_t status) override
  {
    Serial.printf(
      "PEER_PARAMS_UPDATED status=%u interval=%u latency=%u timeout=%u\n",
      static_cast<unsigned>(status), interval, latency, timeout);
  }
};

Callbacks callbacks;

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid ConnParam Peer");
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(&callbacks);
  server->createService(SERVICE_UUID)->start();
  BLEDevice::getAdvertising()->addServiceUUID(SERVICE_UUID);
  BLEDevice::getAdvertising()->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("PEER_READY");
}

void loop()
{
  delay(1);
}
