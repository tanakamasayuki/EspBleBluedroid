#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <esp_bt_device.h>
#include <esp_gap_bt_api.h>
#include <esp_spp_api.h>
#include <esp32-hal-alloc-bt-classic-mem.h>

static constexpr const char *SERVICE_UUID =
  "48e8c100-a176-4c75-8d8d-6f626c756564";
static constexpr const char *CHARACTERISTIC_UUID =
  "48e8c101-a176-4c75-8d8d-6f626c756564";

BLECharacteristic *gattCharacteristic = nullptr;

class DualCharacteristicCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *characteristic) override
  {
    const String value = characteristic->getValue();
    Serial.printf("DUAL_PEER_GATT_WRITE length=%u hex=",
      static_cast<unsigned>(value.length()));
    for (size_t index = 0; index < value.length(); ++index)
    {
      Serial.printf("%02x", static_cast<uint8_t>(value[index]));
    }
    Serial.println();
  }
};

DualCharacteristicCallbacks gattCallbacks;

void printReady()
{
  const uint8_t *address = esp_bt_dev_get_address();
  Serial.printf("DUAL_PEER_READY address=%02x:%02x:%02x:%02x:%02x:%02x\n",
    address[0], address[1], address[2],
    address[3], address[4], address[5]);
}

void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *parameter)
{
  if (parameter == nullptr) return;
  if (event == ESP_SPP_INIT_EVT &&
      parameter->init.status == ESP_SPP_SUCCESS)
  {
    esp_bt_gap_set_device_name("Bluedroid Dual Peer");
    esp_bt_gap_set_scan_mode(
      ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    esp_spp_start_srv(
      ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, "Dual Mode SPP");
  }
  else if (event == ESP_SPP_START_EVT &&
           parameter->start.status == ESP_SPP_SUCCESS)
  {
    printReady();
  }
  else if (event == ESP_SPP_SRV_OPEN_EVT &&
           parameter->srv_open.status == ESP_SPP_SUCCESS)
  {
    Serial.println("DUAL_PEER_SPP_CONNECTED");
  }
  else if (event == ESP_SPP_DATA_IND_EVT)
  {
    Serial.printf("DUAL_PEER_SPP_RX length=%u hex=",
      static_cast<unsigned>(parameter->data_ind.len));
    for (size_t index = 0; index < parameter->data_ind.len; ++index)
    {
      Serial.printf("%02x", parameter->data_ind.data[index]);
    }
    Serial.println();
    static uint8_t response[] = {0xd1, 0x00, 'P'};
    esp_spp_write(
      parameter->data_ind.handle, sizeof(response), response);
  }
  else if (event == ESP_SPP_CLOSE_EVT)
  {
    Serial.println("DUAL_PEER_SPP_DISCONNECTED");
  }
}

void initializePeer()
{
  if (!BLEDevice::init("Bluedroid Dual Peer"))
  {
    Serial.println("DUAL_PEER_INIT_FAILED");
    return;
  }
  BLEServer *server = BLEDevice::createServer();
  BLEService *service = server->createService(SERVICE_UUID);
  gattCharacteristic = service->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_NOTIFY);
  gattCharacteristic->setCallbacks(&gattCallbacks);
  gattCharacteristic->addDescriptor(new BLE2902());
  const uint8_t initialValue[] = {0xb0, 0x00, 0x52};
  gattCharacteristic->setValue(initialValue, sizeof(initialValue));
  service->start();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  if (esp_spp_register_callback(sppCallback) != ESP_OK)
  {
    Serial.println("DUAL_PEER_INIT_FAILED");
    return;
  }
  esp_spp_cfg_t config = BT_SPP_DEFAULT_CONFIG();
  config.mode = ESP_SPP_MODE_CB;
  if (esp_spp_enhanced_init(&config) != ESP_OK)
  {
    Serial.println("DUAL_PEER_INIT_FAILED");
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);
}

void loop()
{
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == 'i')
    {
      initializePeer();
    }
    else if (command == 'n' && gattCharacteristic != nullptr)
    {
      const uint8_t value[] = {0xb2, 0x00, 0x4e};
      gattCharacteristic->setValue(value, sizeof(value));
      gattCharacteristic->notify();
      Serial.println("DUAL_PEER_GATT_NOTIFIED");
    }
  }
  delay(1);
}
