#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <esp_gap_ble_api.h>

static constexpr const char *SERVICE_UUID =
  "e20ab920-8f4a-4e1d-9003-736563757269";
static constexpr const char *CHARACTERISTIC_UUID =
  "e20ab921-8f4a-4e1d-9003-736563757269";

bool secured = false;

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
  success = success && esp_ble_get_bond_device_num() == 0;
  return success;
}

class SecurityCallbacks : public BLESecurityCallbacks
{
  void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override
  {
    secured = result.success;
    Serial.printf(
      "PERIPHERAL_SECURITY success=%u encrypted=%u authenticated=%u bonded=%u key=16\n",
      result.success ? 1 : 0, result.success ? 1 : 0,
      result.success && (result.auth_mode & ESP_LE_AUTH_REQ_MITM) ? 1 : 0,
      result.success && (result.auth_mode & ESP_LE_AUTH_BOND) ? 1 : 0);
  }
};

class ServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *) override
  {
    secured = false;
    Serial.println("PERIPHERAL_CONNECTED");
  }

  void onDisconnect(BLEServer *) override
  {
    Serial.printf("PERIPHERAL_DISCONNECTED secured=%u\n", secured ? 1 : 0);
    secured = false;
  }
};

class CharacteristicCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *characteristic) override
  {
    Serial.printf("SECURE_PEER_WRITE value=%s secured=%u\n",
      characteristic->getValue().c_str(), secured ? 1 : 0);
  }
};

SecurityCallbacks securityCallbacks;
ServerCallbacks serverCallbacks;
CharacteristicCallbacks characteristicCallbacks;

void startAdvertising()
{
  BLEDevice::startAdvertising();
  Serial.println("PERIPHERAL_ADVERTISING");
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid Security Peripheral");
  static BLESecurity security;
  BLESecurity::setCapability(ESP_IO_CAP_NONE);
  BLESecurity::setAuthenticationMode(true, false, true);
  BLESecurity::setForceAuthentication(true);
  BLEDevice::setSecurityCallbacks(&securityCallbacks);

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(&serverCallbacks);
  BLEService *service = server->createService(SERVICE_UUID);
  BLECharacteristic *characteristic = service->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_READ_ENC |
      BLECharacteristic::PROPERTY_WRITE_ENC);
  characteristic->setCallbacks(&characteristicCallbacks);
  characteristic->setValue("secure-ready");
  service->start();
  BLEDevice::getAdvertising()->addServiceUUID(SERVICE_UUID);
  BLEDevice::getAdvertising()->setScanResponse(true);
  startAdvertising();
  Serial.println("SECURITY_PERIPHERAL_READY");
}

void loop()
{
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == 'x')
    {
      const bool cleared = clearBonds();
      Serial.printf("PERIPHERAL_BONDS_CLEARED success=%u count=%d\n",
        cleared ? 1 : 0, esp_ble_get_bond_device_num());
    }
    else if (command == 'b')
    {
      Serial.printf("PERIPHERAL_BONDS count=%d\n",
        esp_ble_get_bond_device_num());
    }
    else if (command == 'a')
    {
      startAdvertising();
    }
  }
  delay(1);
}
