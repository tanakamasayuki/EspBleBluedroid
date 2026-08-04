// Raw Arduino-ESP32 Bluedroid central checking who publishes GATT Service
// Changed on the device under test.

#include <BLEDevice.h>
#include <BLERemoteCharacteristic.h>
#include <BLEScan.h>

static constexpr const char *GENERIC_ATTRIBUTE_SERVICE_UUID =
  "00001801-0000-1000-8000-00805f9b34fb";
static constexpr const char *SERVICE_CHANGED_UUID =
  "00002a05-0000-1000-8000-00805f9b34fb";
static constexpr const char *MARKER_SERVICE_UUID =
  "00020000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *MARKER_CHARACTERISTIC_UUID =
  "00020001-b1dd-4d00-9e5a-627564726f69";

BLEClient *client;

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid Service Changed Peer");
  Serial.println("SERVICE_CHANGED_PEER_READY");
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
  for (int index = 0; results != nullptr && index < results->getCount(); ++index)
  {
    BLEAdvertisedDevice device = results->getDevice(index);
    if (device.haveServiceUUID() &&
        device.isAdvertisingService(BLEUUID(MARKER_SERVICE_UUID)))
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

  // The marker service proves the application's own database is reachable.
  BLERemoteService *marker = client->getService(MARKER_SERVICE_UUID);
  BLERemoteCharacteristic *markerCharacteristic = marker == nullptr
    ? nullptr : marker->getCharacteristic(MARKER_CHARACTERISTIC_UUID);
  const String markerValue = markerCharacteristic == nullptr
    ? String() : markerCharacteristic->readValue();
  Serial.printf("PEER_MARKER found=%u length=%u\n",
    markerCharacteristic != nullptr ? 1 : 0,
    static_cast<unsigned>(markerValue.length()));

  // Generic Attribute is published by the stack even though the application
  // registered no such service.
  BLERemoteService *generic = client->getService(GENERIC_ATTRIBUTE_SERVICE_UUID);
  BLERemoteCharacteristic *serviceChanged = generic == nullptr
    ? nullptr : generic->getCharacteristic(SERVICE_CHANGED_UUID);
  Serial.printf(
    "PEER_SERVICE_CHANGED service=%u characteristic=%u indicatable=%u handle=%u\n",
    generic != nullptr ? 1 : 0, serviceChanged != nullptr ? 1 : 0,
    serviceChanged != nullptr && serviceChanged->canIndicate() ? 1 : 0,
    serviceChanged != nullptr ? serviceChanged->getHandle() : 0);
  Serial.println("PEER_DONE");
}
