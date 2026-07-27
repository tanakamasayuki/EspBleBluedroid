#include <Arduino.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_spp_api.h>
#include <esp32-hal-alloc-bt-classic-mem.h>
#include <esp32-hal-bt.h>

esp_bd_addr_t serverAddress = {};
uint8_t serverChannels[2] = {};
uint32_t handles[2] = {};
bool responses[2] = {};
size_t connectionCount = 0;
bool secondConnectPending = false;
bool disconnectPending = false;

int slotForHandle(uint32_t handle)
{
  for (size_t index = 0; index < 2; ++index)
  {
    if (handles[index] == handle) return static_cast<int>(index);
  }
  return -1;
}

bool parseAddress(const String &hex)
{
  if (hex.length() != 12) return false;
  for (size_t index = 0; index < ESP_BD_ADDR_LEN; ++index)
  {
    char byteText[3] = {
      hex[index * 2], hex[index * 2 + 1], '\0'};
    char *end = nullptr;
    const long value = strtol(byteText, &end, 16);
    if (end == byteText || *end != '\0') return false;
    serverAddress[index] = static_cast<uint8_t>(value);
  }
  return true;
}

void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *parameter)
{
  if (parameter == nullptr) return;
  if (event == ESP_SPP_INIT_EVT &&
      parameter->init.status == ESP_SPP_SUCCESS)
  {
    Serial.println("SPP_MULTI_CLIENT_READY");
  }
  else if (event == ESP_SPP_DISCOVERY_COMP_EVT)
  {
    if (parameter->disc_comp.status != ESP_SPP_SUCCESS ||
        parameter->disc_comp.scn_num < 2)
    {
      Serial.println("SPP_MULTI_CLIENT_DISCOVERY_FAILED");
      return;
    }
    serverChannels[0] = parameter->disc_comp.scn[0];
    serverChannels[1] = parameter->disc_comp.scn[1];
    esp_spp_connect(
      ESP_SPP_SEC_NONE, ESP_SPP_ROLE_MASTER,
      serverChannels[0], serverAddress);
  }
  else if (event == ESP_SPP_OPEN_EVT &&
           parameter->open.status == ESP_SPP_SUCCESS)
  {
    int slot = -1;
    for (size_t index = 0; index < 2; ++index)
    {
      if (handles[index] == 0)
      {
        slot = static_cast<int>(index);
        handles[index] = parameter->open.handle;
        ++connectionCount;
        break;
      }
    }
    if (slot < 0)
    {
      esp_spp_disconnect(parameter->open.handle);
      return;
    }
    Serial.printf(
      "SPP_MULTI_CLIENT_CONNECTED slot=%u handle=%u count=%u\n",
      static_cast<unsigned>(slot + 1),
      static_cast<unsigned>(parameter->open.handle),
      static_cast<unsigned>(connectionCount));
    if (connectionCount == 1)
    {
      secondConnectPending = true;
    }
    else if (connectionCount == 2)
    {
      for (size_t index = 0; index < 2; ++index)
      {
        const uint8_t request[] = {
          static_cast<uint8_t>(index + 1),
          static_cast<uint8_t>('Q')};
        esp_spp_write(
          handles[index], sizeof(request),
          const_cast<uint8_t *>(request));
      }
    }
  }
  else if (event == ESP_SPP_DATA_IND_EVT)
  {
    const int slot = slotForHandle(parameter->data_ind.handle);
    if (slot < 0 || parameter->data_ind.len != 2) return;
    const uint8_t marker = parameter->data_ind.data[0];
    responses[slot] =
      marker == static_cast<uint8_t>(slot + 1) &&
      parameter->data_ind.data[1] == 'R';
    Serial.printf(
      "SPP_MULTI_CLIENT_RX slot=%u marker=%u length=%u\n",
      static_cast<unsigned>(slot + 1),
      static_cast<unsigned>(marker),
      static_cast<unsigned>(parameter->data_ind.len));
    if (responses[0] && responses[1])
    {
      Serial.println("SPP_MULTI_CLIENT_BOTH_RESPONSES");
      disconnectPending = true;
    }
  }
  else if (event == ESP_SPP_CLOSE_EVT)
  {
    const int slot = slotForHandle(parameter->close.handle);
    if (slot < 0) return;
    handles[slot] = 0;
    if (connectionCount > 0) --connectionCount;
    Serial.printf(
      "SPP_MULTI_CLIENT_DISCONNECTED slot=%u count=%u\n",
      static_cast<unsigned>(slot + 1),
      static_cast<unsigned>(connectionCount));
    if (connectionCount == 0)
    {
      Serial.println("SPP_MULTI_CLIENT_IDLE count=0");
    }
  }
}

void initializeClient()
{
  if (!btStartMode(BT_MODE_CLASSIC_BT) ||
      esp_bluedroid_init() != ESP_OK ||
      esp_bluedroid_enable() != ESP_OK ||
      esp_spp_register_callback(sppCallback) != ESP_OK)
  {
    Serial.println("SPP_MULTI_CLIENT_INIT_FAILED");
    return;
  }
  esp_spp_cfg_t config = BT_SPP_DEFAULT_CONFIG();
  config.mode = ESP_SPP_MODE_CB;
  if (esp_spp_enhanced_init(&config) != ESP_OK)
  {
    Serial.println("SPP_MULTI_CLIENT_INIT_FAILED");
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
      initializeClient();
    }
    else if (command == 'c')
    {
      const String address = Serial.readStringUntil('\n');
      if (!parseAddress(address) ||
          esp_spp_start_discovery(serverAddress) != ESP_OK)
      {
        Serial.println("SPP_MULTI_CLIENT_DISCOVERY_FAILED");
      }
    }
  }
  if (secondConnectPending)
  {
    secondConnectPending = false;
    esp_spp_connect(
      ESP_SPP_SEC_NONE, ESP_SPP_ROLE_MASTER,
      serverChannels[1], serverAddress);
  }
  if (disconnectPending)
  {
    disconnectPending = false;
    for (uint32_t handle : handles)
    {
      if (handle != 0) esp_spp_disconnect(handle);
    }
  }
  delay(1);
}
