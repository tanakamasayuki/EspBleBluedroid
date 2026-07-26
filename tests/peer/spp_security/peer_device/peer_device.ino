#include <Arduino.h>
#include <esp_bt.h>
#include <esp_bt_device.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <esp_spp_api.h>
#include <esp32-hal-alloc-bt-classic-mem.h>
#include <esp32-hal-bt.h>

esp_bd_addr_t serverAddress = {};
esp_bd_addr_t comparisonAddress = {};
bool comparisonPending = false;
uint32_t connectionHandle = 0;

String localAddress()
{
  const uint8_t *address = esp_bt_dev_get_address();
  char value[18];
  snprintf(value, sizeof(value), "%02x:%02x:%02x:%02x:%02x:%02x",
    address[0], address[1], address[2], address[3], address[4], address[5]);
  return String(value);
}

size_t clearClassicBonds()
{
  const int count = esp_bt_gap_get_bond_device_num();
  if (count <= 0) return 0;
  esp_bd_addr_t *addresses =
    new esp_bd_addr_t[static_cast<size_t>(count)];
  int actual = count;
  if (esp_bt_gap_get_bond_device_list(&actual, addresses) != ESP_OK)
  {
    delete[] addresses;
    return 0;
  }
  size_t removed = 0;
  for (int index = 0; index < actual; ++index)
  {
    if (esp_bt_gap_remove_bond_device(addresses[index]) == ESP_OK) ++removed;
  }
  delete[] addresses;
  return removed;
}

void gapCallback(
  esp_bt_gap_cb_event_t event,
  esp_bt_gap_cb_param_t *parameter)
{
  if (parameter == nullptr) return;
  if (event == ESP_BT_GAP_CFM_REQ_EVT)
  {
    memcpy(comparisonAddress, parameter->cfm_req.bda, ESP_BD_ADDR_LEN);
    comparisonPending = true;
    Serial.printf("SPP_SECURITY_RAW_COMPARE value=%06u\n",
      static_cast<unsigned>(parameter->cfm_req.num_val));
  }
  else if (event == ESP_BT_GAP_AUTH_CMPL_EVT)
  {
    Serial.printf("SPP_SECURITY_RAW_AUTH success=%u status=%d\n",
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
    Serial.printf("SPP_SECURITY_RAW_READY bonds_cleared=%u\n",
      static_cast<unsigned>(clearClassicBonds()));
  }
  else if (event == ESP_SPP_DISCOVERY_COMP_EVT)
  {
    if (parameter->disc_comp.status != ESP_SPP_SUCCESS ||
        parameter->disc_comp.scn_num == 0)
    {
      Serial.println("SPP_SECURITY_RAW_DISCOVERY_FAILED");
      return;
    }
    esp_spp_connect(
      static_cast<esp_spp_sec_t>(
        ESP_SPP_SEC_AUTHENTICATE | ESP_SPP_SEC_ENCRYPT),
      ESP_SPP_ROLE_MASTER, parameter->disc_comp.scn[0], serverAddress);
  }
  else if (event == ESP_SPP_OPEN_EVT)
  {
    if (parameter->open.status != ESP_SPP_SUCCESS)
    {
      Serial.printf("SPP_SECURITY_RAW_OPEN_FAILED status=%d\n",
        parameter->open.status);
      return;
    }
    connectionHandle = parameter->open.handle;
    Serial.println("SPP_SECURITY_RAW_CONNECTED");
    static uint8_t data[] = {0x00, 0x53, 0xff};
    esp_spp_write(connectionHandle, sizeof(data), data);
  }
  else if (event == ESP_SPP_START_EVT)
  {
    if (parameter->start.status == ESP_SPP_SUCCESS)
    {
      Serial.printf("SPP_SECURITY_RAW_SERVER_READY address=%s\n",
        localAddress().c_str());
    }
    else
    {
      Serial.printf("SPP_SECURITY_RAW_SERVER_START_FAILED status=%d\n",
        parameter->start.status);
    }
  }
  else if (event == ESP_SPP_SRV_OPEN_EVT)
  {
    if (parameter->srv_open.status != ESP_SPP_SUCCESS)
    {
      Serial.printf("SPP_SECURITY_RAW_SERVER_OPEN_FAILED status=%d\n",
        parameter->srv_open.status);
      esp_bt_gap_set_scan_mode(
        ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
      return;
    }
    connectionHandle = parameter->srv_open.handle;
    Serial.println("SPP_SECURITY_RAW_SERVER_CONNECTED");
    static uint8_t data[] = {0x00, 0x43, 0xff};
    esp_spp_write(connectionHandle, sizeof(data), data);
  }
  else if (event == ESP_SPP_DATA_IND_EVT)
  {
    Serial.printf("SPP_SECURITY_RAW_RX length=%u hex=",
      static_cast<unsigned>(parameter->data_ind.len));
    for (size_t index = 0; index < parameter->data_ind.len; ++index)
    {
      Serial.printf("%02x", parameter->data_ind.data[index]);
    }
    Serial.println();
    if (connectionHandle != 0)
    {
      esp_spp_disconnect(parameter->data_ind.handle);
    }
  }
  else if (event == ESP_SPP_CLOSE_EVT)
  {
    connectionHandle = 0;
    Serial.println("SPP_SECURITY_RAW_DISCONNECTED");
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
    Serial.println("SPP_SECURITY_RAW_INIT_FAILED");
    return;
  }
  esp_bt_io_cap_t capability = ESP_BT_IO_CAP_IO;
  if (esp_bt_gap_set_security_param(
        ESP_BT_SP_IOCAP_MODE, &capability, sizeof(capability)) != ESP_OK)
  {
    Serial.println("SPP_SECURITY_RAW_INIT_FAILED");
    return;
  }
  esp_spp_cfg_t config = BT_SPP_DEFAULT_CONFIG();
  config.mode = ESP_SPP_MODE_CB;
  if (esp_spp_enhanced_init(&config) != ESP_OK)
  {
    Serial.println("SPP_SECURITY_RAW_INIT_FAILED");
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
      Serial.println("SPP_SECURITY_RAW_DISCOVERY_FAILED");
    }
  }
  else if (command == 's')
  {
    esp_bt_gap_set_device_name("Raw Secure SPP Server");
    if (
      esp_bt_gap_set_scan_mode(
        ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE) != ESP_OK ||
      esp_spp_start_srv(
        static_cast<esp_spp_sec_t>(
          ESP_SPP_SEC_AUTHENTICATE | ESP_SPP_SEC_ENCRYPT),
        ESP_SPP_ROLE_SLAVE, 0, "Raw Secure SPP Server") != ESP_OK)
    {
      Serial.println("SPP_SECURITY_RAW_SERVER_START_FAILED");
    }
  }
  else if (command == 'b')
  {
    const size_t removed = clearClassicBonds();
    const uint32_t startedAt = millis();
    while (
      esp_bt_gap_get_bond_device_num() != 0 &&
      static_cast<uint32_t>(millis() - startedAt) < 2000)
    {
      delay(10);
    }
    Serial.printf("SPP_SECURITY_RAW_BONDS_CLEARED removed=%u count=%d\n",
      static_cast<unsigned>(removed),
      esp_bt_gap_get_bond_device_num());
  }
  else if ((command == 'a' || command == 'r') && comparisonPending)
  {
    const bool accept = command == 'a';
    const esp_err_t result =
      esp_bt_gap_ssp_confirm_reply(comparisonAddress, accept);
    comparisonPending = false;
    Serial.printf("SPP_SECURITY_RAW_CONFIRM accepted=%u reply=%u\n",
      accept ? 1 : 0, result == ESP_OK ? 1 : 0);
  }
}
