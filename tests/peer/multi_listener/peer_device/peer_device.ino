// A raw Arduino-ESP32 BLE central that makes the listener lists fire for real:
// it connects, subscribes to the CCCD, writes, and receives the notification.

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLERemoteCharacteristic.h>

static constexpr const char *SERVICE_UUID =
  "00080000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *CHARACTERISTIC_UUID =
  "00080001-b1dd-4d00-9e5a-627564726f69";

BLEClient *client = nullptr;
BLERemoteCharacteristic *characteristic = nullptr;

void notificationCallback(
  BLERemoteCharacteristic *, uint8_t *data, size_t length, bool)
{
  Serial.printf("PEER_NOTIFICATION value=%.*s\n", static_cast<int>(length),
    reinterpret_cast<const char *>(data));
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid Multi Listener Peer");
  Serial.println("MULTI_LISTENER_PEER_READY");
}

void loop()
{
  if (!Serial.available())
  {
    delay(1);
    return;
  }
  const int command = Serial.read();
  if (command == 'c')
  {
    BLEScan *scan = BLEDevice::getScan();
    scan->setActiveScan(true);
    BLEScanResults *results = scan->start(5, false);
    BLEAdvertisedDevice *target = nullptr;
    for (int index = 0; results != nullptr && index < results->getCount();
         ++index)
    {
      BLEAdvertisedDevice device = results->getDevice(index);
      if (device.haveServiceUUID() &&
          device.isAdvertisingService(BLEUUID(SERVICE_UUID)))
      {
        target = new BLEAdvertisedDevice(device);
        break;
      }
    }
    if (target == nullptr)
    {
      Serial.println("PEER_TARGET_NOT_FOUND");
      return;
    }
    client = BLEDevice::createClient();
    if (!client->connect(target))
    {
      Serial.println("PEER_CONNECT_FAILED");
      return;
    }
    BLERemoteService *service = client->getService(SERVICE_UUID);
    characteristic = service == nullptr
      ? nullptr : service->getCharacteristic(CHARACTERISTIC_UUID);
    Serial.printf("PEER_CONNECTED characteristic=%u\n",
      characteristic != nullptr ? 1 : 0);
  }
  else if (command == 's')
  {
    if (characteristic == nullptr)
    {
      Serial.println("PEER_NOT_CONNECTED");
      return;
    }
    characteristic->registerForNotify(notificationCallback, true);
    Serial.println("PEER_SUBSCRIBED");
  }
  else if (command == 'w')
  {
    if (characteristic == nullptr)
    {
      Serial.println("PEER_NOT_CONNECTED");
      return;
    }
    characteristic->writeValue(String("peer-write"), true);
    Serial.println("PEER_WRITTEN");
  }
  else if (command == 'x')
  {
    if (client != nullptr) client->disconnect();
    Serial.println("PEER_DISCONNECT_REQUESTED");
  }
}
