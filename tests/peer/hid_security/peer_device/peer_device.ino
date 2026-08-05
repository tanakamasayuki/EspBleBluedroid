// A raw Arduino-ESP32 BLE central standing in for a HID host OS, in two states: one
// that refuses to pair and one that accepts.
//
// Refusing is what makes the unpaired case reachable at all. The first version of
// this suite simply connected without configuring any security parameters and tried
// to read — but the device requests security as soon as a host connects (the
// peripheral-initiated Security Request HOGP devices are allowed to send), and the
// wrapper's default is to accept it, so the link was encrypted before any read
// happened. A host whose user dismisses the pairing dialog is the real unpaired
// case, and onSecurityRequest() returning false is exactly that.
//
// Reading with this library on the other end would only show this library's own view
// of the attribute table, so the instrument is deliberately the wrapper's raw client.
//
// Bonds outlive a flash, so both boards clear theirs before the unpaired phase.

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLERemoteCharacteristic.h>
#include <BLESecurity.h>
#include <esp_gap_ble_api.h>

static constexpr const char *HID_SERVICE_UUID = "1812";
static constexpr const char *REPORT_MAP_UUID = "2a4b";
static constexpr const char *HID_INFORMATION_UUID = "2a4a";
static constexpr const char *REPORT_UUID = "2a4d";
static constexpr const char *REPORT_REFERENCE_UUID = "2908";
static constexpr const char *TARGET_NAME = "Bluedroid HID 0011";

BLEClient *client = nullptr;
BLERemoteService *hidService = nullptr;
bool secured = false;

void printHex(const uint8_t *data, size_t length)
{
  for (size_t index = 0; index < length; ++index) Serial.printf("%02x", data[index]);
}

bool clearBonds()
{
  int count = esp_ble_get_bond_device_num();
  if (count <= 0) return true;
  esp_ble_bond_dev_t *bonds = new esp_ble_bond_dev_t[count];
  if (bonds == nullptr) return false;
  int listed = count;
  bool success = esp_ble_get_bond_device_list(&listed, bonds) == ESP_OK;
  for (int index = 0; success && index < listed; ++index)
  {
    success = esp_ble_remove_bond_device(bonds[index].bd_addr) == ESP_OK;
  }
  delete[] bonds;
  const uint32_t startedAt = millis();
  while (success && esp_ble_get_bond_device_num() != 0 &&
         static_cast<uint32_t>(millis() - startedAt) < 2000)
  {
    delay(10);
  }
  return success && esp_ble_get_bond_device_num() == 0;
}

// Whether this central accepts the device's Security Request. False stands for a
// user who dismissed the pairing dialog.
bool acceptPairing = false;

class SecurityCallbacks : public BLESecurityCallbacks
{
  bool onSecurityRequest() override
  {
    Serial.printf("PEER_SECURITY_REQUEST accepted=%u\n", acceptPairing ? 1 : 0);
    return acceptPairing;
  }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override
  {
    secured = result.success;
    Serial.printf("PEER_SECURITY success=%u bonded=%u\n",
      result.success ? 1 : 0,
      result.success && (result.auth_mode & ESP_LE_AUTH_BOND) ? 1 : 0);
  }
};

// A bonded Just Works pairing is what this central is willing to do when it accepts
// at all: a keyboard has no display and no keypad, so nothing stronger is available.
void configureSecurity()
{
  BLEDevice::setSecurityCallbacks(new SecurityCallbacks());
  BLESecurity *security = new BLESecurity();
  security->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  security->setCapability(ESP_IO_CAP_NONE);
  security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
}

bool connectToTarget()
{
  BLEScan *scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  BLEScanResults *results = scan->start(5, false);
  BLEAdvertisedDevice *target = nullptr;
  for (int index = 0; results != nullptr && index < results->getCount(); ++index)
  {
    BLEAdvertisedDevice device = results->getDevice(index);
    if (device.haveName() && device.getName() == String(TARGET_NAME))
    {
      target = new BLEAdvertisedDevice(device);
      break;
    }
  }
  if (target == nullptr)
  {
    Serial.println("PEER_TARGET_NOT_FOUND");
    return false;
  }
  client = BLEDevice::createClient();
  if (!client->connect(target))
  {
    Serial.println("PEER_CONNECT_FAILED");
    return false;
  }
  hidService = client->getService(HID_SERVICE_UUID);
  Serial.printf("PEER_CONNECTED hid=%u secured=%u\n",
    hidService != nullptr ? 1 : 0, secured ? 1 : 0);
  return true;
}

// Read one HID attribute and report the length. A refused read comes back empty
// through this API, so the length is the observable: 0 means the attribute did not
// answer, and the Report Map is never legitimately empty.
void readAttribute(const char *label, const char *uuid)
{
  BLERemoteCharacteristic *characteristic =
    hidService == nullptr ? nullptr : hidService->getCharacteristic(uuid);
  if (characteristic == nullptr)
  {
    Serial.printf("PEER_%s missing=1\n", label);
    return;
  }
  const String value = characteristic->readValue();
  Serial.printf("PEER_%s length=%u hex=", label,
    static_cast<unsigned>(value.length()));
  printHex(reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
  Serial.println();
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid HID Security Peer");
  // Installed up front, because the refusal has to be in place before the first
  // connection: the device sends its Security Request immediately.
  configureSecurity();
  Serial.println("HID_SECURITY_PEER_READY");
}

void loop()
{
  if (!Serial.available())
  {
    delay(1);
    return;
  }
  const int command = Serial.read();
  if (command == 'C')
  {
    Serial.printf("PEER_BONDS_CLEARED success=%u count=%u\n",
      clearBonds() ? 1 : 0, esp_ble_get_bond_device_num());
  }
  else if (command == 'S' || command == 'R')
  {
    acceptPairing = command == 'S';
    secured = false;
    Serial.printf("PEER_PAIRING_MODE accept=%u\n", acceptPairing ? 1 : 0);
  }
  else if (command == 'c')
  {
    connectToTarget();
  }
  else if (command == 'm')
  {
    readAttribute("REPORT_MAP", REPORT_MAP_UUID);
  }
  else if (command == 'i')
  {
    readAttribute("HID_INFORMATION", HID_INFORMATION_UUID);
  }
  else if (command == 'f')
  {
    // A Report Reference descriptor under a 0x2A4D characteristic. Every Input
    // Report's reference is protected too, not only the characteristics: a host
    // that could still read these would learn the report layout unencrypted.
    if (hidService == nullptr)
    {
      Serial.println("PEER_NOT_CONNECTED");
      return;
    }
    hidService->getCharacteristics();
    std::map<uint16_t, BLERemoteCharacteristic *> *byHandle =
      hidService->getCharacteristicsByHandle();
    unsigned reports = 0;
    unsigned readable = 0;
    for (auto &entry : *byHandle)
    {
      if (!entry.second->getUUID().equals(BLEUUID(REPORT_UUID))) continue;
      ++reports;
      BLERemoteDescriptor *reference =
        entry.second->getDescriptor(BLEUUID(REPORT_REFERENCE_UUID));
      if (reference == nullptr) continue;
      if (reference->readValue().length() == 2) ++readable;
    }
    Serial.printf("PEER_REFERENCES reports=%u readable=%u\n", reports, readable);
  }
  else if (command == 'x')
  {
    if (client != nullptr) client->disconnect();
    hidService = nullptr;
    Serial.println("PEER_DISCONNECTED");
  }
  else if (command == '?')
  {
    Serial.printf("PEER_STATE connected=%u secured=%u bonds=%u\n",
      client != nullptr && client->isConnected() ? 1 : 0, secured ? 1 : 0,
      esp_ble_get_bond_device_num());
  }
}
