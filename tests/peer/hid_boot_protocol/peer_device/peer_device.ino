// A raw Arduino-ESP32 BLE central standing in for a boot-protocol HID host: it finds
// the Report-protocol Input Report and the Boot Keyboard Input Report, subscribes to
// each independently, writes Protocol Mode to switch the device between them, and
// prints every notification with the handle it arrived on — which is the only way to
// tell "the same keystroke, over the other characteristic" from a wrong report.
//
// It raises the MTU after connecting, because an NKRO Input Report is 29 bytes and
// an ATT payload is MTU - 3.
//
// Deliberately not this library: what is being checked is what a *host* can see, so
// the instrument must not share this library's idea of the attribute table.

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLERemoteCharacteristic.h>

static constexpr const char *HID_SERVICE_UUID = "1812";
static constexpr const char *PROTOCOL_MODE_UUID = "2a4e";
static constexpr const char *REPORT_UUID = "2a4d";
static constexpr const char *REPORT_REFERENCE_UUID = "2908";
static constexpr const char *BOOT_KEYBOARD_INPUT_UUID = "2a22";
static constexpr const char *BOOT_KEYBOARD_OUTPUT_UUID = "2a32";
static constexpr const char *TARGET_NAME = "Bluedroid HID 000f";
static constexpr uint16_t REQUESTED_MTU = 247;

static constexpr uint8_t REPORT_TYPE_INPUT = 0x01;
static constexpr uint8_t BOOT_PROTOCOL_MODE = 0x00;
static constexpr uint8_t REPORT_PROTOCOL_MODE = 0x01;

BLEClient *client = nullptr;
BLERemoteService *hidService = nullptr;
BLERemoteCharacteristic *reportInput = nullptr;
BLERemoteCharacteristic *bootInput = nullptr;
BLERemoteCharacteristic *bootOutput = nullptr;
BLERemoteCharacteristic *protocolMode = nullptr;

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

void writeProtocolMode(uint8_t mode)
{
  if (protocolMode == nullptr)
  {
    Serial.println("PEER_PROTOCOL_MODE_NOT_FOUND");
    return;
  }
  // Protocol Mode is Write Without Response, the way HOGP defines it.
  protocolMode->writeValue(&mode, 1, false);
  Serial.printf("PEER_MODE_WRITTEN mode=%u\n", mode);
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid HID Host Peer");
  Serial.println("HID_BOOT_PROTOCOL_PEER_READY");
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
    // A 29-byte NKRO report needs an ATT payload that large.
    client->setMTU(REQUESTED_MTU);
    hidService = client->getService(HID_SERVICE_UUID);
    Serial.printf("PEER_CONNECTED hid=%u\n", hidService != nullptr ? 1 : 0);
  }
  else if (command == 'd')
  {
    if (hidService == nullptr)
    {
      Serial.println("PEER_NOT_CONNECTED");
      return;
    }
    // The Report-protocol Input Report is the 0x2A4D whose Report Reference says
    // Input; the boot reports have UUIDs of their own, which is the whole point of
    // Boot Protocol — a host that cannot parse a Report Map can still find them.
    hidService->getCharacteristics();
    std::map<uint16_t, BLERemoteCharacteristic *> *byHandle =
      hidService->getCharacteristicsByHandle();
    reportInput = nullptr;
    for (auto &entry : *byHandle)
    {
      if (!entry.second->getUUID().equals(BLEUUID(REPORT_UUID))) continue;
      BLERemoteDescriptor *reference =
        entry.second->getDescriptor(BLEUUID(REPORT_REFERENCE_UUID));
      if (reference == nullptr) continue;
      const String value = reference->readValue();
      if (value.length() == 2 && static_cast<uint8_t>(value[1]) == REPORT_TYPE_INPUT)
      {
        reportInput = entry.second;
      }
    }
    bootInput = hidService->getCharacteristic(BOOT_KEYBOARD_INPUT_UUID);
    bootOutput = hidService->getCharacteristic(BOOT_KEYBOARD_OUTPUT_UUID);
    protocolMode = hidService->getCharacteristic(PROTOCOL_MODE_UUID);
    Serial.printf(
      "PEER_DISCOVERED report=%u boot_in=%u boot_out=%u mode=%u\n",
      reportInput == nullptr ? 0
                             : static_cast<unsigned>(reportInput->getHandle()),
      bootInput == nullptr ? 0 : static_cast<unsigned>(bootInput->getHandle()),
      bootOutput == nullptr ? 0 : static_cast<unsigned>(bootOutput->getHandle()),
      protocolMode == nullptr ? 0
                              : static_cast<unsigned>(protocolMode->getHandle()));
  }
  else if (command == 'p')
  {
    if (protocolMode == nullptr)
    {
      Serial.println("PEER_PROTOCOL_MODE_NOT_FOUND");
      return;
    }
    const String value = protocolMode->readValue();
    Serial.printf("PEER_MODE_READ length=%u value=%u\n",
      static_cast<unsigned>(value.length()),
      value.length() == 1 ? static_cast<unsigned>(value[0]) : 0xff);
  }
  else if (command == 's')
  {
    if (reportInput == nullptr)
    {
      Serial.println("PEER_NOT_DISCOVERED");
      return;
    }
    reportInput->registerForNotify(notificationCallback, true);
    Serial.println("PEER_REPORT_SUBSCRIBED");
  }
  else if (command == 'S')
  {
    if (bootInput == nullptr)
    {
      Serial.println("PEER_NOT_DISCOVERED");
      return;
    }
    bootInput->registerForNotify(notificationCallback, true);
    Serial.println("PEER_BOOT_SUBSCRIBED");
  }
  else if (command == 'u')
  {
    if (bootInput == nullptr)
    {
      Serial.println("PEER_NOT_DISCOVERED");
      return;
    }
    // Turning the Boot Keyboard CCCD off is what proves ready() follows the report
    // the selected Protocol Mode actually uses.
    bootInput->registerForNotify(nullptr, true);
    Serial.println("PEER_BOOT_UNSUBSCRIBED");
  }
  else if (command == 'b')
  {
    writeProtocolMode(BOOT_PROTOCOL_MODE);
  }
  else if (command == 'B')
  {
    writeProtocolMode(REPORT_PROTOCOL_MODE);
  }
  else if (command == 'o')
  {
    if (bootOutput == nullptr)
    {
      Serial.println("PEER_NOT_DISCOVERED");
      return;
    }
    uint8_t leds = 0x02;  // Caps Lock
    bootOutput->writeValue(&leds, 1, true);
    Serial.println("PEER_BOOT_OUTPUT_WRITTEN");
  }
  else if (command == 'x')
  {
    if (client != nullptr) client->disconnect();
    Serial.println("PEER_DISCONNECT_REQUESTED");
  }
}
