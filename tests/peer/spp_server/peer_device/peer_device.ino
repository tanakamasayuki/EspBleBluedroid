#include <Arduino.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <esp_spp_api.h>
#include <esp32-hal-alloc-bt-classic-mem.h>
#include <esp32-hal-bt.h>

esp_bd_addr_t serverAddress = {};

void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *parameter)
{
  if (parameter == nullptr) return;
  if (event == ESP_SPP_INIT_EVT &&
      parameter->init.status == ESP_SPP_SUCCESS)
  {
    Serial.println("SPP_RAW_CLIENT_READY");
  }
  else if (event == ESP_SPP_DISCOVERY_COMP_EVT)
  {
    if (parameter->disc_comp.status != ESP_SPP_SUCCESS ||
        parameter->disc_comp.scn_num == 0)
    {
      Serial.println("SPP_RAW_DISCOVERY_FAILED");
      return;
    }
    esp_spp_connect(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_MASTER,
      parameter->disc_comp.scn[0], serverAddress);
  }
  else if (event == ESP_SPP_OPEN_EVT &&
           parameter->open.status == ESP_SPP_SUCCESS)
  {
    Serial.println("SPP_RAW_CONNECTED");
    static uint8_t message[] = {0x00, 0x7f, 'P'};
    esp_spp_write(parameter->open.handle, sizeof(message), message);
  }
  else if (event == ESP_SPP_DATA_IND_EVT)
  {
    Serial.printf("SPP_RAW_RX length=%u hex=",
      static_cast<unsigned>(parameter->data_ind.len));
    for (size_t index = 0; index < parameter->data_ind.len; ++index)
    {
      Serial.printf("%02x", parameter->data_ind.data[index]);
    }
    Serial.println();
    esp_spp_disconnect(parameter->data_ind.handle);
  }
  else if (event == ESP_SPP_CLOSE_EVT)
  {
    Serial.println("SPP_RAW_DISCONNECTED");
  }
}

bool parseAddress(const String &hex)
{
  if (hex.length() != 12) return false;
  for (size_t index = 0; index < ESP_BD_ADDR_LEN; ++index)
  {
    char byteText[3] = {hex[index * 2], hex[index * 2 + 1], '\0'};
    char *end = nullptr;
    const long value = strtol(byteText, &end, 16);
    if (end == byteText || *end != '\0') return false;
    serverAddress[index] = static_cast<uint8_t>(value);
  }
  return true;
}

void initializeClient()
{
  if (!btStartMode(BT_MODE_CLASSIC_BT) ||
      esp_bluedroid_init() != ESP_OK ||
      esp_bluedroid_enable() != ESP_OK ||
      esp_spp_register_callback(sppCallback) != ESP_OK)
  {
    Serial.println("SPP_RAW_INIT_FAILED");
    return;
  }
  esp_spp_cfg_t config = BT_SPP_DEFAULT_CONFIG();
  config.mode = ESP_SPP_MODE_CB;
  if (esp_spp_enhanced_init(&config) != ESP_OK)
  {
    Serial.println("SPP_RAW_INIT_FAILED");
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
        Serial.println("SPP_RAW_DISCOVERY_FAILED");
      }
    }
  }
  delay(1);
}
