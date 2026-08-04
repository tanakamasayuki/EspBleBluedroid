// A raw Arduino-ESP32 BLE central that addresses two same-UUID characteristics on
// the library under test.
//
// The wrapper's own UUID-keyed map (`getCharacteristics()`) can only return one of
// a duplicated pair, which is exactly why this sketch walks
// `getCharacteristicsByHandle()` instead: the handle map holds both. That is also
// the honest statement of what a client has to do to work with duplicates, and it
// is why the instrument here is the raw wrapper rather than a second copy of the
// library under test — a shared assumption about the pair would cancel itself out.

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLERemoteCharacteristic.h>

static constexpr const char *SERVICE_UUID =
  "000b0000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *CHARACTERISTIC_UUID =
  "000b0001-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *DESCRIPTOR_UUID =
  "000b0002-b1dd-4d00-9e5a-627564726f69";

BLEClient *client = nullptr;
BLERemoteService *service = nullptr;
BLERemoteCharacteristic *first = nullptr;
BLERemoteCharacteristic *second = nullptr;

void notificationCallback(
  BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length, bool)
{
  Serial.printf("PEER_NOTIFICATION handle=%u value=%.*s\n",
    static_cast<unsigned>(characteristic->getHandle()),
    static_cast<int>(length), reinterpret_cast<const char *>(data));
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid Duplicate Server Peer");
  Serial.println("DUPLICATE_SERVER_PEER_READY");
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
    service = client->getService(SERVICE_UUID);
    Serial.printf("PEER_CONNECTED service=%u\n", service != nullptr ? 1 : 0);
  }
  else if (command == 'd')
  {
    if (service == nullptr)
    {
      Serial.println("PEER_NOT_CONNECTED");
      return;
    }
    // Populate the maps, then read the handle-keyed one: the UUID-keyed map holds
    // one entry per UUID and would hide the second characteristic entirely.
    service->getCharacteristics();
    std::map<uint16_t, BLERemoteCharacteristic *> *byHandle =
      service->getCharacteristicsByHandle();
    first = nullptr;
    second = nullptr;
    unsigned matches = 0;
    for (auto &entry : *byHandle)
    {
      if (!entry.second->getUUID().equals(BLEUUID(CHARACTERISTIC_UUID))) continue;
      ++matches;
      if (first == nullptr) first = entry.second;
      else if (second == nullptr) second = entry.second;
    }
    Serial.printf("PEER_DISCOVERY matches=%u distinct=%u first=%u second=%u\n",
      matches,
      first != nullptr && second != nullptr &&
        first->getHandle() != second->getHandle() ? 1 : 0,
      first == nullptr ? 0 : static_cast<unsigned>(first->getHandle()),
      second == nullptr ? 0 : static_cast<unsigned>(second->getHandle()));
  }
  else if (command == 'r')
  {
    if (first == nullptr || second == nullptr)
    {
      Serial.println("PEER_NOT_DISCOVERED");
      return;
    }
    // Different values behind one UUID: the proof that the database really carries
    // two attributes and not one reached twice.
    const String firstValue = first->readValue();
    const String secondValue = second->readValue();
    Serial.printf("PEER_READ first=%s second=%s\n", firstValue.c_str(),
      secondValue.c_str());
  }
  else if (command == 'D')
  {
    if (first == nullptr || second == nullptr)
    {
      Serial.println("PEER_NOT_DISCOVERED");
      return;
    }
    BLERemoteDescriptor *firstDescriptor =
      first->getDescriptor(BLEUUID(DESCRIPTOR_UUID));
    BLERemoteDescriptor *secondDescriptor =
      second->getDescriptor(BLEUUID(DESCRIPTOR_UUID));
    if (firstDescriptor == nullptr || secondDescriptor == nullptr)
    {
      Serial.println("PEER_DESCRIPTOR_NOT_FOUND");
      return;
    }
    Serial.printf("PEER_DESCRIPTOR first=%s second=%s\n",
      firstDescriptor->readValue().c_str(),
      secondDescriptor->readValue().c_str());
  }
  else if (command == 'w')
  {
    // Written to the second one only, so the server has to attribute it to the
    // second handle rather than to the UUID's first match.
    if (second == nullptr)
    {
      Serial.println("PEER_NOT_DISCOVERED");
      return;
    }
    second->writeValue(String("peer-to-second"), true);
    Serial.println("PEER_WRITTEN");
  }
  else if (command == 's')
  {
    if (first == nullptr || second == nullptr)
    {
      Serial.println("PEER_NOT_DISCOVERED");
      return;
    }
    first->registerForNotify(notificationCallback, true);
    second->registerForNotify(notificationCallback, true);
    Serial.println("PEER_SUBSCRIBED");
  }
  else if (command == 'x')
  {
    if (client != nullptr) client->disconnect();
    Serial.println("PEER_DISCONNECT_REQUESTED");
  }
}
