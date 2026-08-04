// A raw Arduino-ESP32 BLE central for the peripheral-link scenario: it connects to
// the library under test, raises the MTU, pairs, reads an encrypted
// characteristic and writes a plain one, then disconnects.
//
// Deliberately not this library: what the scenario checks is what the library
// reports about a link *someone else* opened, and two halves of the same library
// would share any wrong assumption about the peripheral role.

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLERemoteCharacteristic.h>
#include <BLESecurity.h>
#include <esp_gap_ble_api.h>

static constexpr const char *SERVICE_UUID =
  "00070000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *PLAIN_UUID =
  "00070001-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *ENCRYPTED_UUID =
  "00070002-b1dd-4d00-9e5a-627564726f69";

BLEClient *client = nullptr;
BLERemoteService *service = nullptr;
esp_bd_addr_t peerAddress{};
bool peerAddressPresent = false;

void setup()
{
  Serial.begin(115200);
  delay(500);
  // 247 on this side too, so the negotiated value the peripheral reports is the
  // one both sides asked for.
  BLEDevice::init("Bluedroid Peripheral Link Peer");
  BLEDevice::setMTU(247);
  BLESecurity security;
  security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  security.setCapability(ESP_IO_CAP_NONE);
  security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  Serial.println("PERIPHERAL_LINK_PEER_READY");
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
    memcpy(peerAddress, target->getAddress().getNative(), sizeof(peerAddress));
    peerAddressPresent = true;
    client = BLEDevice::createClient();
    if (!client->connect(target))
    {
      Serial.println("PEER_CONNECT_FAILED");
      return;
    }
    service = client->getService(SERVICE_UUID);
    Serial.printf("PEER_CONNECTED service=%u\n", service != nullptr ? 1 : 0);
  }
  else if (command == 'm')
  {
    // Ask for the exchange explicitly: connect() requests it too, but the
    // response has not arrived by the time connect() returns, so the value would
    // be read before the negotiation finished.
    if (client == nullptr)
    {
      Serial.println("PEER_NOT_CONNECTED");
      return;
    }
    const bool requested = client->setMTU(247);
    Serial.printf("PEER_MTU requested=%u mtu=%u\n", requested ? 1 : 0,
      static_cast<unsigned>(client->getMTU()));
  }
  else if (command == 'p')
  {
    // Pair explicitly rather than relying on the encrypted read to provoke it, so
    // the step that fails is unambiguous.
    if (!peerAddressPresent)
    {
      Serial.println("PEER_NOT_CONNECTED");
      return;
    }
    const esp_err_t status =
      esp_ble_set_encryption(peerAddress, ESP_BLE_SEC_ENCRYPT_NO_MITM);
    Serial.printf("PEER_PAIR_REQUESTED %u\n", status == ESP_OK ? 1 : 0);
  }
  else if (command == 'r')
  {
    BLERemoteCharacteristic *characteristic =
      service == nullptr ? nullptr : service->getCharacteristic(ENCRYPTED_UUID);
    if (characteristic == nullptr)
    {
      Serial.println("PEER_ENCRYPTED_NOT_FOUND");
      return;
    }
    const String value = characteristic->readValue();
    Serial.printf("PEER_ENCRYPTED_READ value=%s\n", value.c_str());
  }
  else if (command == 'w')
  {
    BLERemoteCharacteristic *characteristic =
      service == nullptr ? nullptr : service->getCharacteristic(PLAIN_UUID);
    if (characteristic == nullptr)
    {
      Serial.println("PEER_PLAIN_NOT_FOUND");
      return;
    }
    characteristic->writeValue(String("peer-write"), true);
    Serial.println("PEER_WRITTEN");
  }
  else if (command == 'z')
  {
    // Both sides have to forget: this peer keeping keys the peripheral has
    // deleted makes the next encryption attempt fail instead of pairing again.
    // Removal is applied through the stack, so the count is re-read until it
    // settles rather than once: a bond left here fails the next encryption
    // instead of pairing again.
    int removed = 0;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
      const int count = esp_ble_get_bond_device_num();
      if (count <= 0) break;
      esp_ble_bond_dev_t *bonds = new esp_ble_bond_dev_t[count];
      int listed = count;
      if (esp_ble_get_bond_device_list(&listed, bonds) == ESP_OK)
      {
        for (int index = 0; index < listed; ++index)
        {
          if (esp_ble_remove_bond_device(bonds[index].bd_addr) == ESP_OK)
          {
            ++removed;
          }
        }
      }
      delete[] bonds;
      delay(100);
    }
    Serial.printf("PEER_BONDS_CLEARED removed=%d count=%d\n", removed,
      esp_ble_get_bond_device_num());
  }
  else if (command == 'x')
  {
    if (client != nullptr)
    {
      client->disconnect();
    }
    Serial.println("PEER_DISCONNECT_REQUESTED");
  }
}
