#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLERemoteCharacteristic.h>
#include <BLERemoteDescriptor.h>

static constexpr const char *SERVICE_UUID =
  "6b976b10-5e89-4e3f-8a94-676174747372";
static constexpr const char *CHARACTERISTIC_UUID =
  "6b976b11-5e89-4e3f-8a94-676174747372";
static constexpr const char *DESCRIPTOR_UUID =
  "6b976b12-5e89-4e3f-8a94-676174747372";

BLEClient *client;

void notificationCallback(
  BLERemoteCharacteristic *, uint8_t *data, size_t length, bool isNotify)
{
  Serial.printf("PEER_NOTIFICATION length=%u hex=",
    static_cast<unsigned>(length));
  for (size_t i = 0; i < length; ++i) Serial.printf("%02x", data[i]);
  Serial.printf(" indication=%u\n", isNotify ? 0 : 1);
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid GATT Server Peer");
  Serial.println("GATT_SERVER_PEER_READY");
}

void loop()
{
  if (!Serial.available() || Serial.read() != 'c')
  {
    delay(1);
    return;
  }
  BLEScan *scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  BLEScanResults *results = scan->start(5, false);
  BLEAdvertisedDevice *target = nullptr;
  for (int i = 0; results != nullptr && i < results->getCount(); ++i)
  {
    BLEAdvertisedDevice device = results->getDevice(i);
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
  BLERemoteCharacteristic *characteristic =
    service == nullptr ? nullptr : service->getCharacteristic(CHARACTERISTIC_UUID);
  BLERemoteDescriptor *descriptor = characteristic == nullptr ? nullptr :
    characteristic->getDescriptor(BLEUUID(DESCRIPTOR_UUID));
  if (characteristic == nullptr || descriptor == nullptr)
  {
    Serial.println("PEER_DISCOVERY_FAILED");
    return;
  }
  const String readValue = characteristic->readValue();
  Serial.printf("PEER_READ length=%u hex=", static_cast<unsigned>(readValue.length()));
  for (size_t i = 0; i < readValue.length(); ++i)
    Serial.printf("%02x", static_cast<uint8_t>(readValue[i]));
  Serial.println();
  const uint8_t writeValue[] = {0x57, 0x00, 0xfc};
  characteristic->writeValue(
    const_cast<uint8_t *>(writeValue), sizeof(writeValue), true);
  const uint8_t descriptorValue[] = {0x45, 0x00, 0xfb};
  descriptor->writeValue(
    const_cast<uint8_t *>(descriptorValue), sizeof(descriptorValue), true);
  characteristic->registerForNotify(notificationCallback, true);
  Serial.println("PEER_SUBSCRIBED");
}
