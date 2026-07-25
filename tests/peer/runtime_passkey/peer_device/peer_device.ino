#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <esp_gap_ble_api.h>

static constexpr const char *MARKER_SERVICE_UUID = "1815";

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
    success = esp_ble_remove_bond_device(bonds[index].bd_addr) == ESP_OK;
  delete[] bonds;
  const uint32_t startedAt = millis();
  while (success && esp_ble_get_bond_device_num() != 0 &&
         static_cast<uint32_t>(millis() - startedAt) < 2000)
    delay(10);
  return success && esp_ble_get_bond_device_num() == 0;
}

class SecurityCallbacks : public BLESecurityCallbacks
{
  void onPassKeyNotify(uint32_t passkey) override
  {
    Serial.printf("RUNTIME_PASSKEY_DISPLAYED passkey=%06u\n",
      static_cast<unsigned>(passkey));
  }

  void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override
  {
    authenticated = result.success &&
      (result.auth_mode & ESP_LE_AUTH_REQ_MITM) != 0;
    Serial.printf(
      "RUNTIME_PEER_SECURITY success=%u encrypted=%u authenticated=%u bonded=%u key=16\n",
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
    Serial.println("RUNTIME_PASSKEY_PEER_CONNECTED");
  }

  void onDisconnect(BLEServer *) override
  {
    Serial.printf("RUNTIME_PASSKEY_PEER_DISCONNECTED authenticated=%u\n",
      authenticated ? 1 : 0);
  }
};

SecurityCallbacks securityCallbacks;
ServerCallbacks serverCallbacks;

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid Runtime Passkey Peer");
  static BLESecurity security;
  BLESecurity::setCapability(ESP_IO_CAP_OUT);
  BLESecurity::setPassKey(false);
  BLESecurity::regenPassKeyOnConnect(true);
  BLESecurity::setAuthenticationMode(true, true, true);
  BLESecurity::setForceAuthentication(true);
  BLEDevice::setSecurityCallbacks(&securityCallbacks);

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(&serverCallbacks);
  BLEService *service = server->createService(MARKER_SERVICE_UUID);
  service->createCharacteristic(
    "2ae2", BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_READ_AUTHEN)->setValue("runtime");
  service->start();
  BLEDevice::getAdvertising()->addServiceUUID(MARKER_SERVICE_UUID);
  BLEDevice::getAdvertising()->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("RUNTIME_PASSKEY_PEER_READY");
}

void loop()
{
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == 'x')
    {
      const bool cleared = clearBonds();
      Serial.printf("RUNTIME_PEER_BONDS_CLEARED success=%u count=%d\n",
        cleared ? 1 : 0, esp_ble_get_bond_device_num());
    }
    else if (command == 'a')
    {
      BLEDevice::startAdvertising();
      Serial.println("RUNTIME_PASSKEY_PEER_ADVERTISING");
    }
  }
  delay(1);
}
