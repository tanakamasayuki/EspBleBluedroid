#include <Arduino.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <esp32-hal-alloc-bt-classic-mem.h>
#include <esp32-hal-bt.h>

void setup()
{
  Serial.begin(115200);
  delay(500);
  if (!btStartMode(BT_MODE_CLASSIC_BT))
  {
    Serial.println("CLASSIC_PEER_CONTROLLER_FAILED");
    return;
  }
  if (esp_bluedroid_init() != ESP_OK ||
      esp_bluedroid_enable() != ESP_OK)
  {
    Serial.println("CLASSIC_PEER_HOST_FAILED");
    return;
  }
  esp_bt_gap_set_device_name("Bluedroid Classic Peer");
  esp_bt_cod_t cod = {};
  cod.major = ESP_BT_COD_MAJOR_DEV_COMPUTER;
  cod.minor = 0;
  cod.service = ESP_BT_COD_SRVC_CAPTURING;
  esp_bt_gap_set_cod(cod, ESP_BT_INIT_COD);
  if (esp_bt_gap_set_scan_mode(
        ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE) != ESP_OK)
  {
    Serial.println("CLASSIC_PEER_DISCOVERABLE_FAILED");
    return;
  }
  Serial.println("CLASSIC_PEER_READY");
}

void loop()
{
  delay(1);
}
