#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <esp_gap_ble_api.h>

static constexpr const char *SERVICE_UUID =
  "35c6a570-a63d-44a2-9003-706173736b79";
static constexpr const char *CHARACTERISTIC_UUID =
  "35c6a571-a63d-44a2-9003-706173736b79";
static constexpr uint32_t PASSKEY = 438209;

bool authenticated = false;

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

class SecurityCallbacks : public BLESecurityCallbacks
{
  uint32_t onPassKeyRequest() override
  {
    return PASSKEY;
  }

  void onPassKeyNotify(uint32_t passkey) override
  {
    Serial.printf("PEER_PASSKEY_DISPLAYED passkey=%06u\n",
      static_cast<unsigned>(passkey));
  }

  void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override
  {
    authenticated = result.success &&
      (result.auth_mode & ESP_LE_AUTH_REQ_MITM) != 0;
    Serial.printf(
      "PEER_PASSKEY_SECURITY success=%u encrypted=%u authenticated=%u bonded=%u key=16\n",
      result.success ? 1 : 0, result.success ? 1 : 0,
      authenticated ? 1 : 0,
      result.success && (result.auth_mode & ESP_LE_AUTH_BOND) ? 1 : 0);
  }
};

class ServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *) override
  {
    authenticated = false;
    Serial.println("PASSKEY_PEER_CONNECTED");
  }

  void onDisconnect(BLEServer *) override
  {
    Serial.printf("PASSKEY_PEER_DISCONNECTED authenticated=%u\n",
      authenticated ? 1 : 0);
  }
};

class CharacteristicCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *characteristic) override
  {
    Serial.printf("AUTHENTICATED_PEER_WRITE value=%s authenticated=%u\n",
      characteristic->getValue().c_str(), authenticated ? 1 : 0);
  }
};

SecurityCallbacks securityCallbacks;
ServerCallbacks serverCallbacks;
CharacteristicCallbacks characteristicCallbacks;

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid Passkey Peripheral");
  static BLESecurity security;
  BLESecurity::setCapability(ESP_IO_CAP_IN);
  BLESecurity::setPassKey(true, PASSKEY);
  BLESecurity::setAuthenticationMode(true, true, true);
  BLESecurity::setForceAuthentication(true);
  BLEDevice::setSecurityCallbacks(&securityCallbacks);

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(&serverCallbacks);
  BLEService *service = server->createService(SERVICE_UUID);
  BLECharacteristic *characteristic = service->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_READ_AUTHEN |
      BLECharacteristic::PROPERTY_WRITE_AUTHEN);
  characteristic->setCallbacks(&characteristicCallbacks);
  characteristic->setValue("authenticated-ready");
  service->start();
  BLEDevice::getAdvertising()->addServiceUUID(SERVICE_UUID);
  BLEDevice::getAdvertising()->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("PASSKEY_PEER_READY");
}

void loop()
{
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == 'x')
    {
      const bool cleared = clearBonds();
      Serial.printf("PASSKEY_PEER_BONDS_CLEARED success=%u count=%d\n",
        cleared ? 1 : 0, esp_ble_get_bond_device_num());
    }
    else if (command == 'b')
    {
      Serial.printf("PASSKEY_PEER_BONDS count=%d\n",
        esp_ble_get_bond_device_num());
    }
  }
  delay(1);
}
