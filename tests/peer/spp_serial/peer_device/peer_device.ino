#include <Arduino.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_spp_api.h>
#include <esp32-hal-alloc-bt-classic-mem.h>
#include <esp32-hal-bt.h>

esp_bd_addr_t serverAddress = {};
uint8_t received[1010] = {};
size_t receivedLength = 0;

void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *parameter)
{
  if (parameter == nullptr) return;
  if (event == ESP_SPP_INIT_EVT &&
      parameter->init.status == ESP_SPP_SUCCESS)
  {
    Serial.println("SPP_SERIAL_RAW_READY");
  }
  else if (event == ESP_SPP_DISCOVERY_COMP_EVT)
  {
    if (parameter->disc_comp.status != ESP_SPP_SUCCESS ||
        parameter->disc_comp.scn_num == 0)
    {
      Serial.println("SPP_SERIAL_RAW_DISCOVERY_FAILED");
      return;
    }
    esp_spp_connect(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_MASTER,
      parameter->disc_comp.scn[0], serverAddress);
  }
  else if (event == ESP_SPP_OPEN_EVT &&
           parameter->open.status == ESP_SPP_SUCCESS)
  {
    receivedLength = 0;
    static uint8_t request[] = {0x00, 'A', '\n'};
    Serial.println("SPP_SERIAL_RAW_CONNECTED");
    esp_spp_write(parameter->open.handle, sizeof(request), request);
  }
  else if (event == ESP_SPP_DATA_IND_EVT)
  {
    const size_t remaining = sizeof(received) - receivedLength;
    const size_t length =
      parameter->data_ind.len < remaining
      ? parameter->data_ind.len
      : remaining;
    memcpy(received + receivedLength, parameter->data_ind.data, length);
    receivedLength += length;
    if (receivedLength != sizeof(received)) return;
    bool prefixMatches =
      memcmp(received, "value=42\r\n", 10) == 0;
    uint32_t checksum = 0;
    bool binaryMatches = true;
    for (size_t index = 0; index < 1000; ++index)
    {
      const uint8_t expected = static_cast<uint8_t>(index % 251);
      checksum += received[10 + index];
      if (received[10 + index] != expected) binaryMatches = false;
    }
    Serial.printf(
      "SPP_SERIAL_RAW_RX length=%u prefix=%u binary=%u checksum=%u\n",
      static_cast<unsigned>(receivedLength), prefixMatches ? 1 : 0,
      binaryMatches ? 1 : 0, static_cast<unsigned>(checksum));
    esp_spp_disconnect(parameter->data_ind.handle);
  }
  else if (event == ESP_SPP_CLOSE_EVT)
  {
    Serial.println("SPP_SERIAL_RAW_DISCONNECTED");
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
    Serial.println("SPP_SERIAL_RAW_INIT_FAILED");
    return;
  }
  esp_spp_cfg_t config = BT_SPP_DEFAULT_CONFIG();
  config.mode = ESP_SPP_MODE_CB;
  if (esp_spp_enhanced_init(&config) != ESP_OK)
  {
    Serial.println("SPP_SERIAL_RAW_INIT_FAILED");
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
    if (command == 'i') initializeClient();
    else if (command == 'c')
    {
      const String address = Serial.readStringUntil('\n');
      if (!parseAddress(address) ||
          esp_spp_start_discovery(serverAddress) != ESP_OK)
      {
        Serial.println("SPP_SERIAL_RAW_DISCOVERY_FAILED");
      }
    }
  }
  delay(1);
}
