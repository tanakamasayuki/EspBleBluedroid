#include <Arduino.h>
#include <esp_bt.h>
#include <esp_bt_device.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <esp_spp_api.h>
#include <esp32-hal-alloc-bt-classic-mem.h>
#include <esp32-hal-bt.h>

void printLocalAddress()
{
  const uint8_t *address = esp_bt_dev_get_address();
  Serial.printf("SPP_RAW_SERVER_READY address=%02x:%02x:%02x:%02x:%02x:%02x\n",
    address[0], address[1], address[2],
    address[3], address[4], address[5]);
}

void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *parameter)
{
  if (parameter == nullptr) return;
  if (event == ESP_SPP_INIT_EVT &&
      parameter->init.status == ESP_SPP_SUCCESS)
  {
    esp_bt_gap_set_device_name("Raw SPP Server");
    esp_bt_gap_set_scan_mode(
      ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    esp_spp_start_srv(
      ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, "Raw SPP Server");
  }
  else if (event == ESP_SPP_START_EVT &&
           parameter->start.status == ESP_SPP_SUCCESS)
  {
    printLocalAddress();
  }
  else if (event == ESP_SPP_SRV_OPEN_EVT &&
           parameter->srv_open.status == ESP_SPP_SUCCESS)
  {
    Serial.println("SPP_RAW_SERVER_CONNECTED");
    static uint8_t message[] = {0x01, 0x00, 'R'};
    esp_spp_write(parameter->srv_open.handle, sizeof(message), message);
  }
  else if (event == ESP_SPP_DATA_IND_EVT)
  {
    Serial.printf("SPP_RAW_SERVER_RX length=%u hex=",
      static_cast<unsigned>(parameter->data_ind.len));
    for (size_t index = 0; index < parameter->data_ind.len; ++index)
    {
      Serial.printf("%02x", parameter->data_ind.data[index]);
    }
    Serial.println();
  }
  else if (event == ESP_SPP_CLOSE_EVT)
  {
    Serial.println("SPP_RAW_SERVER_DISCONNECTED");
  }
  else if (event == ESP_SPP_SRV_STOP_EVT)
  {
    Serial.println("SPP_RAW_SERVER_STOPPED");
  }
}

void initializeServer()
{
  if (!btStartMode(BT_MODE_CLASSIC_BT) ||
      esp_bluedroid_init() != ESP_OK ||
      esp_bluedroid_enable() != ESP_OK ||
      esp_spp_register_callback(sppCallback) != ESP_OK)
  {
    Serial.println("SPP_RAW_SERVER_INIT_FAILED");
    return;
  }
  esp_spp_cfg_t config = BT_SPP_DEFAULT_CONFIG();
  config.mode = ESP_SPP_MODE_CB;
  if (esp_spp_enhanced_init(&config) != ESP_OK)
  {
    Serial.println("SPP_RAW_SERVER_INIT_FAILED");
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
    if (command == 'i') initializeServer();
    else if (command == 'x') esp_spp_stop_srv();
  }
  delay(1);
}
