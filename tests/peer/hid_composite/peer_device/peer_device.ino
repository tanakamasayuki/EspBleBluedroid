// A raw Arduino-ESP32 BLE central standing in for a HID host OS, for the composite
// device: it enumerates every Report characteristic by handle, reads each Report
// Reference to learn which report it is, subscribes to all of them, and prints each
// notification with the handle it arrived on. That is how a host keeps five
// profiles apart when they share UUID 0x2A4D inside one HID service.
//
// It walks the HID service the way a host does: read the Report Map, read the HID
// Information, tell the two 0x2A4D Report characteristics apart by their Report
// Reference descriptors, subscribe to the input report, write the LED output
// report, and write the Protocol Mode. The handle-keyed characteristic map is used
// for the pair, because the wrapper's UUID-keyed map can only return one of two
// characteristics sharing a UUID — which is the whole shape HOGP relies on.
//
// Deliberately not this library: what is being checked is what a *host* can see, so
// the instrument must not share this library's idea of the attribute table.

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLERemoteCharacteristic.h>

static constexpr const char *HID_SERVICE_UUID = "1812";
static constexpr const char *REPORT_MAP_UUID = "2a4b";
static constexpr const char *HID_INFORMATION_UUID = "2a4a";
static constexpr const char *PROTOCOL_MODE_UUID = "2a4e";
static constexpr const char *REPORT_UUID = "2a4d";
static constexpr const char *REPORT_REFERENCE_UUID = "2908";
static constexpr const char *BATTERY_SERVICE_UUID = "180f";
static constexpr const char *BATTERY_LEVEL_UUID = "2a19";
static constexpr const char *DEVICE_INFORMATION_UUID = "180a";
static constexpr const char *PNP_ID_UUID = "2a50";
static constexpr const char *MANUFACTURER_UUID = "2a29";
static constexpr const char *TARGET_NAME = "Bluedroid HID 000d";

BLEClient *client = nullptr;
BLERemoteService *hidService = nullptr;
// The Report characteristics found by handle, with the report each one declares.
struct ReportEntry
{
  BLERemoteCharacteristic *characteristic = nullptr;
  uint8_t reportId = 0;
  uint8_t reportType = 0;
};
static constexpr size_t MaxReports = 8;
ReportEntry reports[MaxReports];
size_t reportCount = 0;

void printHex(const uint8_t *data, size_t length)
{
  for (size_t index = 0; index < length; ++index) Serial.printf("%02x", data[index]);
}

void notificationCallback(
  BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length, bool)
{
  Serial.printf("PEER_INPUT handle=%u length=%u hex=",
    static_cast<unsigned>(characteristic->getHandle()),
    static_cast<unsigned>(length));
  printHex(data, length);
  Serial.println();
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid HID Host Peer");
  Serial.println("HID_KEYBOARD_PEER_READY");
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
          device.isAdvertisingService(BLEUUID(HID_SERVICE_UUID)) &&
          device.haveName() && device.getName() == String(TARGET_NAME))
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
    hidService = client->getService(HID_SERVICE_UUID);
    Serial.printf("PEER_CONNECTED hid=%u\n", hidService != nullptr ? 1 : 0);
  }
  else if (command == 'm')
  {
    if (hidService == nullptr)
    {
      Serial.println("PEER_NOT_CONNECTED");
      return;
    }
    BLERemoteCharacteristic *map =
      hidService->getCharacteristic(REPORT_MAP_UUID);
    if (map == nullptr)
    {
      Serial.println("PEER_REPORT_MAP_NOT_FOUND");
      return;
    }
    // The Report Map is longer than one ATT packet at the default MTU, so this
    // read exercises the long-read path a host OS uses.
    const String value = map->readValue();
    Serial.printf("PEER_REPORT_MAP length=%u hex=",
      static_cast<unsigned>(value.length()));
    printHex(reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
    Serial.println();
  }
  else if (command == 'i')
  {
    BLERemoteCharacteristic *information =
      hidService == nullptr ? nullptr
                            : hidService->getCharacteristic(HID_INFORMATION_UUID);
    if (information == nullptr)
    {
      Serial.println("PEER_HID_INFORMATION_NOT_FOUND");
      return;
    }
    const String value = information->readValue();
    Serial.print("PEER_HID_INFORMATION hex=");
    printHex(reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
    Serial.println();
  }
  else if (command == 'd')
  {
    if (hidService == nullptr)
    {
      Serial.println("PEER_NOT_CONNECTED");
      return;
    }
    // Every 0x2A4D characteristic, in handle order, with the report it declares.
    hidService->getCharacteristics();
    std::map<uint16_t, BLERemoteCharacteristic *> *byHandle =
      hidService->getCharacteristicsByHandle();
    reportCount = 0;
    for (auto &entry : *byHandle)
    {
      if (!entry.second->getUUID().equals(BLEUUID(REPORT_UUID))) continue;
      BLERemoteDescriptor *reference =
        entry.second->getDescriptor(BLEUUID(REPORT_REFERENCE_UUID));
      if (reference == nullptr || reportCount >= MaxReports) continue;
      const String value = reference->readValue();
      if (value.length() != 2) continue;
      reports[reportCount].characteristic = entry.second;
      reports[reportCount].reportId = static_cast<uint8_t>(value[0]);
      reports[reportCount].reportType = static_cast<uint8_t>(value[1]);
      ++reportCount;
    }
    Serial.printf("PEER_REPORTS count=%u", static_cast<unsigned>(reportCount));
    for (size_t index = 0; index < reportCount; ++index)
    {
      Serial.printf(" %u:%u", reports[index].reportId, reports[index].reportType);
    }
    Serial.println();
  }
  else if (command == 's')
  {
    // Subscribe to every Input Report, so a notification proves which report it
    // came from rather than which one happened to be the only subscription.
    unsigned subscribed = 0;
    for (size_t index = 0; index < reportCount; ++index)
    {
      if (reports[index].reportType != 0x01) continue;
      reports[index].characteristic->registerForNotify(notificationCallback, true);
      ++subscribed;
    }
    Serial.printf("PEER_SUBSCRIBED count=%u\n", subscribed);
  }
  else if (command == 'R')
  {
    // Which report a handle belongs to, so the test can name reports rather than
    // handles in its assertions.
    Serial.printf("PEER_HANDLES count=%u", static_cast<unsigned>(reportCount));
    for (size_t index = 0; index < reportCount; ++index)
    {
      Serial.printf(" %u:%u=%u", reports[index].reportId,
        reports[index].reportType,
        static_cast<unsigned>(reports[index].characteristic->getHandle()));
    }
    Serial.println();
  }
  else if (command == 'o')
  {
    // Caps Lock on, the way a host tells a keyboard to light its LED.
    BLERemoteCharacteristic *output = nullptr;
    for (size_t index = 0; index < reportCount; ++index)
    {
      if (reports[index].reportType == 0x02) output = reports[index].characteristic;
    }
    if (output == nullptr)
    {
      Serial.println("PEER_NOT_DISCOVERED");
      return;
    }
    uint8_t leds = 0x02;
    output->writeValue(&leds, 1, true);
    Serial.println("PEER_OUTPUT_WRITTEN");
  }
  else if (command == 'p')
  {
    BLERemoteCharacteristic *mode =
      hidService == nullptr ? nullptr
                            : hidService->getCharacteristic(PROTOCOL_MODE_UUID);
    if (mode == nullptr)
    {
      Serial.println("PEER_PROTOCOL_MODE_NOT_FOUND");
      return;
    }
    const String before = mode->readValue();
    uint8_t boot = 0x00;
    mode->writeValue(&boot, 1, false);
    Serial.printf("PEER_MODE_WRITTEN before=%u\n",
      before.length() == 1 ? static_cast<unsigned>(before[0]) : 0xff);
  }
  else if (command == 'B')
  {
    BLERemoteService *battery =
      client == nullptr ? nullptr : client->getService(BATTERY_SERVICE_UUID);
    BLERemoteCharacteristic *level =
      battery == nullptr ? nullptr
                         : battery->getCharacteristic(BATTERY_LEVEL_UUID);
    if (level == nullptr)
    {
      Serial.println("PEER_BATTERY_NOT_FOUND");
      return;
    }
    const String value = level->readValue();
    Serial.printf("PEER_BATTERY level=%u\n",
      value.length() == 1 ? static_cast<unsigned>(value[0]) : 0xff);
  }
  else if (command == 'I')
  {
    BLERemoteService *information =
      client == nullptr ? nullptr : client->getService(DEVICE_INFORMATION_UUID);
    BLERemoteCharacteristic *pnp =
      information == nullptr ? nullptr
                             : information->getCharacteristic(PNP_ID_UUID);
    BLERemoteCharacteristic *manufacturer =
      information == nullptr
        ? nullptr : information->getCharacteristic(MANUFACTURER_UUID);
    if (pnp == nullptr || manufacturer == nullptr)
    {
      Serial.println("PEER_DEVICE_INFORMATION_NOT_FOUND");
      return;
    }
    const String pnpValue = pnp->readValue();
    Serial.print("PEER_PNP_ID hex=");
    printHex(reinterpret_cast<const uint8_t *>(pnpValue.c_str()),
      pnpValue.length());
    Serial.printf(" manufacturer=%s\n", manufacturer->readValue().c_str());
  }
  else if (command == 'x')
  {
    if (client != nullptr) client->disconnect();
    Serial.println("PEER_DISCONNECT_REQUESTED");
  }
}
