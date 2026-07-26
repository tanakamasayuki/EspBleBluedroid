#include <Arduino.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <esp_spp_api.h>
#include <esp32-hal-alloc-bt-classic-mem.h>
#include <esp32-hal-bt.h>

esp_bd_addr_t serverAddress = {};
esp_bd_addr_t passkeyAddress = {};
bool passkeyPending = false;

void clearClassicBonds()
{
  const int count = esp_bt_gap_get_bond_device_num();
  esp_bd_addr_t *addresses =
    count > 0 ? new esp_bd_addr_t[static_cast<size_t>(count)] : nullptr;
  int listed = count;
  if (
    addresses != nullptr &&
    esp_bt_gap_get_bond_device_list(&listed, addresses) == ESP_OK)
  {
    for (int index = 0; index < listed; ++index)
    {
      esp_bt_gap_remove_bond_device(addresses[index]);
    }
  }
  delete[] addresses;
  const uint32_t startedAt = millis();
  while (
    esp_bt_gap_get_bond_device_num() != 0 &&
    static_cast<uint32_t>(millis() - startedAt) < 2000)
  {
    delay(10);
  }
}

void gapCallback(
  esp_bt_gap_cb_event_t event,
  esp_bt_gap_cb_param_t *parameter)
{
  if (parameter == nullptr) return;
  if (event == ESP_BT_GAP_KEY_NOTIF_EVT)
  {
    Serial.printf("SPP_PASSKEY_RAW_DISPLAYED passkey=%06u\n",
      static_cast<unsigned>(parameter->key_notif.passkey));
  }
  else if (event == ESP_BT_GAP_KEY_REQ_EVT)
  {
    memcpy(passkeyAddress, parameter->key_req.bda, ESP_BD_ADDR_LEN);
    passkeyPending = true;
    Serial.println("SPP_PASSKEY_RAW_REQUESTED");
  }
  else if (event == ESP_BT_GAP_AUTH_CMPL_EVT)
  {
    Serial.printf("SPP_PASSKEY_RAW_SECURITY success=%u status=%d\n",
      parameter->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS ? 1 : 0,
      parameter->auth_cmpl.stat);
  }
}

void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *parameter)
{
  if (parameter == nullptr) return;
  if (event == ESP_SPP_INIT_EVT &&
      parameter->init.status == ESP_SPP_SUCCESS)
  {
    clearClassicBonds();
    Serial.println("SPP_PASSKEY_RAW_READY");
  }
  else if (event == ESP_SPP_DISCOVERY_COMP_EVT)
  {
    if (parameter->disc_comp.status != ESP_SPP_SUCCESS ||
        parameter->disc_comp.scn_num == 0)
    {
      Serial.println("SPP_PASSKEY_RAW_DISCOVERY_FAILED");
      return;
    }
    esp_spp_connect(
      static_cast<esp_spp_sec_t>(
        ESP_SPP_SEC_AUTHENTICATE | ESP_SPP_SEC_ENCRYPT),
      ESP_SPP_ROLE_MASTER, parameter->disc_comp.scn[0], serverAddress);
  }
  else if (event == ESP_SPP_OPEN_EVT &&
           parameter->open.status == ESP_SPP_SUCCESS)
  {
    Serial.println("SPP_PASSKEY_RAW_CONNECTED");
    static uint8_t value[] = {0x00, 0x50, 0xff};
    esp_spp_write(parameter->open.handle, sizeof(value), value);
  }
  else if (event == ESP_SPP_DATA_IND_EVT)
  {
    Serial.printf("SPP_PASSKEY_RAW_RX length=%u hex=",
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
    Serial.println("SPP_PASSKEY_RAW_DISCONNECTED");
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
      esp_bt_gap_register_callback(gapCallback) != ESP_OK ||
      esp_spp_register_callback(sppCallback) != ESP_OK)
  {
    Serial.println("SPP_PASSKEY_RAW_INIT_FAILED");
    return;
  }
  esp_bt_io_cap_t capability = ESP_BT_IO_CAP_OUT;
  if (esp_bt_gap_set_security_param(
        ESP_BT_SP_IOCAP_MODE, &capability, sizeof(capability)) != ESP_OK)
  {
    Serial.println("SPP_PASSKEY_RAW_INIT_FAILED");
    return;
  }
  esp_spp_cfg_t config = BT_SPP_DEFAULT_CONFIG();
  config.mode = ESP_SPP_MODE_CB;
  if (esp_spp_enhanced_init(&config) != ESP_OK)
  {
    Serial.println("SPP_PASSKEY_RAW_INIT_FAILED");
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);
}

void loop()
{
  if (!Serial.available())
  {
    delay(1);
    return;
  }
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
      Serial.println("SPP_PASSKEY_RAW_DISCOVERY_FAILED");
    }
  }
  else if (command == 'b')
  {
    clearClassicBonds();
    Serial.printf("SPP_PASSKEY_RAW_BONDS count=%d\n",
      esp_bt_gap_get_bond_device_num());
  }
  else if (command == 'm')
  {
    esp_bt_io_cap_t capability = ESP_BT_IO_CAP_IN;
    const esp_err_t result = esp_bt_gap_set_security_param(
      ESP_BT_SP_IOCAP_MODE, &capability, sizeof(capability));
    Serial.printf("SPP_PASSKEY_RAW_KEYBOARD success=%u\n",
      result == ESP_OK ? 1 : 0);
  }
  else if (command == 'k' && passkeyPending)
  {
    const uint32_t passkey =
      static_cast<uint32_t>(Serial.parseInt());
    const esp_err_t result =
      esp_bt_gap_ssp_passkey_reply(passkeyAddress, true, passkey);
    passkeyPending = false;
    Serial.printf(
      "SPP_PASSKEY_RAW_PROVIDED accepted=%u passkey=%06u\n",
      result == ESP_OK ? 1 : 0, static_cast<unsigned>(passkey));
  }
}
