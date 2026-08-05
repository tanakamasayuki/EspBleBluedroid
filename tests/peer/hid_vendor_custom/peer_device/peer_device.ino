// A raw Arduino-ESP32 BLE central standing in for a HID host OS, for the vendor
// and custom profiles: it enumerates every Report characteristic by handle, reads
// each Report Reference to learn the report ID and type, subscribes to the inputs,
// and writes the Output and Feature reports the device declared.
//
// It raises the MTU before connecting, because the vendor reports are 40 bytes and
// an ATT payload is MTU - 3. The negotiated value is printed so the test can tell
// a truncation apart from a wrong report.
//
// Deliberately not this library: what is being checked is what a *host* can see, so
// the instrument must not share this library's idea of the attribute table.

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLERemoteCharacteristic.h>

static constexpr const char *HID_SERVICE_UUID = "1812";
static constexpr const char *REPORT_MAP_UUID = "2a4b";
static constexpr const char *REPORT_UUID = "2a4d";
static constexpr const char *REPORT_REFERENCE_UUID = "2908";
static constexpr const char *TARGET_NAME = "Bluedroid HID 000e";
static constexpr uint16_t REQUESTED_MTU = 247;
static constexpr size_t VENDOR_REPORT_SIZE = 40;

static constexpr uint8_t REPORT_TYPE_INPUT = 0x01;
static constexpr uint8_t REPORT_TYPE_OUTPUT = 0x02;
static constexpr uint8_t REPORT_TYPE_FEATURE = 0x03;
static constexpr uint8_t VENDOR_REPORT_ID = 6;
static constexpr uint8_t CUSTOM_REPORT_ID = 7;

BLEClient *client = nullptr;
BLERemoteService *hidService = nullptr;
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

BLERemoteCharacteristic *reportCharacteristic(uint8_t reportId, uint8_t reportType)
{
  for (size_t index = 0; index < reportCount; ++index)
  {
    if (reports[index].reportId == reportId &&
        reports[index].reportType == reportType)
    {
      return reports[index].characteristic;
    }
  }
  return nullptr;
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

// Write `length` bytes derived from `seed`, with a response or without.
void writeReport(const char *label, uint8_t reportId, uint8_t reportType,
  uint8_t seed, size_t length, bool response)
{
  BLERemoteCharacteristic *characteristic =
    reportCharacteristic(reportId, reportType);
  if (characteristic == nullptr)
  {
    Serial.printf("PEER_%s_NOT_FOUND\n", label);
    return;
  }
  uint8_t value[64];
  for (size_t index = 0; index < length && index < sizeof(value); ++index)
  {
    value[index] = static_cast<uint8_t>(seed + index);
  }
  characteristic->writeValue(value, length, response);
  Serial.printf("PEER_%s_WRITTEN length=%u hex=", label,
    static_cast<unsigned>(length));
  printHex(value, length);
  Serial.println();
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid HID Host Peer");
  // The client exchanges this on connect; a 40-byte report needs at least 43.
  BLEDevice::setMTU(REQUESTED_MTU);
  Serial.println("HID_VENDOR_CUSTOM_PEER_READY");
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
    // The exchange the wrapper sends at connect can be missed while the
    // connection ID is still being assigned, so ask again explicitly: without a
    // raised MTU a 40-byte report would be truncated to the payload limit.
    client->setMTU(REQUESTED_MTU);
    hidService = client->getService(HID_SERVICE_UUID);
    Serial.printf("PEER_CONNECTED hid=%u\n", hidService != nullptr ? 1 : 0);
  }
  else if (command == 'm')
  {
    BLERemoteCharacteristic *map =
      hidService == nullptr ? nullptr
                            : hidService->getCharacteristic(REPORT_MAP_UUID);
    if (map == nullptr)
    {
      Serial.println("PEER_REPORT_MAP_NOT_FOUND");
      return;
    }
    const String value = map->readValue();
    Serial.printf("PEER_REPORT_MAP length=%u hex=",
      static_cast<unsigned>(value.length()));
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
  else if (command == 'R')
  {
    Serial.printf("PEER_HANDLES count=%u", static_cast<unsigned>(reportCount));
    for (size_t index = 0; index < reportCount; ++index)
    {
      Serial.printf(" %u:%u=%u", reports[index].reportId,
        reports[index].reportType,
        static_cast<unsigned>(reports[index].characteristic->getHandle()));
    }
    Serial.println();
  }
  else if (command == 'p')
  {
    // Which write forms each report allows, which is how a host tells an Output
    // report (state, may be sent unacknowledged) from a Feature report
    // (configuration, always acknowledged).
    Serial.printf("PEER_PROPERTIES count=%u", static_cast<unsigned>(reportCount));
    for (size_t index = 0; index < reportCount; ++index)
    {
      Serial.printf(" %u:%u=n%uw%uu%u", reports[index].reportId,
        reports[index].reportType,
        reports[index].characteristic->canNotify() ? 1 : 0,
        reports[index].characteristic->canWrite() ? 1 : 0,
        reports[index].characteristic->canWriteNoResponse() ? 1 : 0);
    }
    Serial.println();
  }
  else if (command == 's')
  {
    unsigned subscribed = 0;
    for (size_t index = 0; index < reportCount; ++index)
    {
      if (reports[index].reportType != REPORT_TYPE_INPUT) continue;
      reports[index].characteristic->registerForNotify(notificationCallback, true);
      ++subscribed;
    }
    Serial.printf("PEER_SUBSCRIBED count=%u\n", subscribed);
  }
  else if (command == 'o')
  {
    // A whole vendor report without a response, which is how a host sends state.
    writeReport("VENDOR_OUTPUT", VENDOR_REPORT_ID, REPORT_TYPE_OUTPUT, 0x40,
      VENDOR_REPORT_SIZE, false);
  }
  else if (command == 'f')
  {
    writeReport("VENDOR_FEATURE", VENDOR_REPORT_ID, REPORT_TYPE_FEATURE, 0x80,
      VENDOR_REPORT_SIZE, true);
  }
  else if (command == 'O')
  {
    writeReport("CUSTOM_OUTPUT", CUSTOM_REPORT_ID, REPORT_TYPE_OUTPUT, 0xa1, 2,
      false);
  }
  else if (command == 'F')
  {
    writeReport("CUSTOM_FEATURE", CUSTOM_REPORT_ID, REPORT_TYPE_FEATURE, 0xb1, 3,
      true);
  }
  else if (command == 'x')
  {
    if (client != nullptr) client->disconnect();
    Serial.println("PEER_DISCONNECT_REQUESTED");
  }
}
