#include <Arduino.h>
#include <esp_bt.h>
#include <esp_bt_device.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <esp_spp_api.h>
#include <esp32-hal-alloc-bt-classic-mem.h>
#include <esp32-hal-bt.h>

uint32_t handles[2] = {};
uint8_t serverChannels[2] = {};
size_t connectionCount = 0;
size_t serverStartCount = 0;

int slotForHandle(uint32_t handle)
{
  for (size_t index = 0; index < 2; ++index)
  {
    if (handles[index] == handle) return static_cast<int>(index);
  }
  return -1;
}

void printReady()
{
  const uint8_t *address = esp_bt_dev_get_address();
  Serial.printf(
    "SPP_MULTI_SERVER_READY "
    "address=%02x:%02x:%02x:%02x:%02x:%02x "
    "acl_max=%u channels=%u,%u\n",
    address[0], address[1], address[2],
    address[3], address[4], address[5],
    static_cast<unsigned>(CONFIG_BTDM_CTRL_BR_EDR_MAX_ACL_CONN),
    static_cast<unsigned>(serverChannels[0]),
    static_cast<unsigned>(serverChannels[1]));
}

void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *parameter)
{
  if (parameter == nullptr) return;
  if (event == ESP_SPP_INIT_EVT &&
      parameter->init.status == ESP_SPP_SUCCESS)
  {
    esp_bt_gap_set_device_name("Bluedroid Multi SPP Server");
    esp_bt_gap_set_scan_mode(
      ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    esp_spp_start_srv(
      ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, "Multi SPP 1");
  }
  else if (event == ESP_SPP_START_EVT &&
           parameter->start.status == ESP_SPP_SUCCESS)
  {
    if (serverStartCount < 2)
    {
      serverChannels[serverStartCount] = parameter->start.scn;
      ++serverStartCount;
    }
    if (serverStartCount == 1)
    {
      esp_spp_start_srv(
        ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, "Multi SPP 2");
    }
    else if (serverStartCount == 2)
    {
      printReady();
    }
  }
  else if (event == ESP_SPP_SRV_OPEN_EVT &&
           parameter->srv_open.status == ESP_SPP_SUCCESS)
  {
    int slot = slotForHandle(parameter->srv_open.handle);
    if (slot < 0)
    {
      for (size_t index = 0; index < 2; ++index)
      {
        if (handles[index] == 0)
        {
          slot = static_cast<int>(index);
          handles[index] = parameter->srv_open.handle;
          ++connectionCount;
          break;
        }
      }
    }
    if (slot < 0)
    {
      esp_spp_disconnect(parameter->srv_open.handle);
      return;
    }
    Serial.printf(
      "SPP_MULTI_SERVER_CONNECTED slot=%u handle=%u count=%u\n",
      static_cast<unsigned>(slot + 1),
      static_cast<unsigned>(parameter->srv_open.handle),
      static_cast<unsigned>(connectionCount));
  }
  else if (event == ESP_SPP_DATA_IND_EVT)
  {
    const int slot = slotForHandle(parameter->data_ind.handle);
    if (slot < 0 || parameter->data_ind.len != 2) return;
    const uint8_t marker = parameter->data_ind.data[0];
    Serial.printf(
      "SPP_MULTI_SERVER_RX slot=%u marker=%u length=%u\n",
      static_cast<unsigned>(slot + 1),
      static_cast<unsigned>(marker),
      static_cast<unsigned>(parameter->data_ind.len));
    const uint8_t response[] = {
      marker, static_cast<uint8_t>('R')};
    esp_spp_write(
      parameter->data_ind.handle, sizeof(response),
      const_cast<uint8_t *>(response));
  }
  else if (event == ESP_SPP_CLOSE_EVT)
  {
    const int slot = slotForHandle(parameter->close.handle);
    if (slot < 0) return;
    handles[slot] = 0;
    if (connectionCount > 0) --connectionCount;
    Serial.printf(
      "SPP_MULTI_SERVER_DISCONNECTED slot=%u count=%u\n",
      static_cast<unsigned>(slot + 1),
      static_cast<unsigned>(connectionCount));
    if (connectionCount == 0)
    {
      Serial.println("SPP_MULTI_SERVER_IDLE count=0");
    }
  }
}

void initializeServer()
{
  if (!btStartMode(BT_MODE_CLASSIC_BT) ||
      esp_bluedroid_init() != ESP_OK ||
      esp_bluedroid_enable() != ESP_OK ||
      esp_spp_register_callback(sppCallback) != ESP_OK)
  {
    Serial.println("SPP_MULTI_SERVER_INIT_FAILED");
    return;
  }
  esp_spp_cfg_t config = BT_SPP_DEFAULT_CONFIG();
  config.mode = ESP_SPP_MODE_CB;
  if (esp_spp_enhanced_init(&config) != ESP_OK)
  {
    Serial.println("SPP_MULTI_SERVER_INIT_FAILED");
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);
}

void loop()
{
  if (Serial.available() && Serial.read() == 'i')
  {
    initializeServer();
  }
  delay(1);
}
