#include <Arduino.h>
#include <esp_a2dp_api.h>
#include <esp_bt_device.h>
#include <esp_gap_bt_api.h>
#include <esp_bt_main.h>
#include <esp32-hal-alloc-bt-classic-mem.h>
#include <esp32-hal-bt.h>

esp_bd_addr_t sinkAddress = {};
esp_a2d_conn_hdl_t connectionHandle = 0;
volatile uint32_t pcmRequestCount = 0;
volatile int32_t lastPcmRequestLength = 0;
volatile bool pcmRequestObserved = false;

void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *parameter)
{
  if (parameter == nullptr) return;
  if (event == ESP_BT_GAP_CFM_REQ_EVT)
    esp_bt_gap_ssp_confirm_reply(parameter->cfm_req.bda, true);
}

String localAddress()
{
  const uint8_t *address = esp_bt_dev_get_address();
  if (address == nullptr) return "";
  char value[18];
  snprintf(value, sizeof(value), "%02x:%02x:%02x:%02x:%02x:%02x",
    address[0], address[1], address[2], address[3], address[4], address[5]);
  return String(value);
}

esp_a2d_mcc_t sbcEndpoint()
{
  esp_a2d_mcc_t result = {};
  result.type = ESP_A2D_MCT_SBC;
  result.cie.sbc_info.samp_freq = ESP_A2D_SBC_CIE_SF_44K;
  result.cie.sbc_info.ch_mode = ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO;
  result.cie.sbc_info.block_len = ESP_A2D_SBC_CIE_BLOCK_LEN_16;
  result.cie.sbc_info.num_subbands = ESP_A2D_SBC_CIE_NUM_SUBBANDS_8;
  result.cie.sbc_info.alloc_mthd = ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS;
  result.cie.sbc_info.min_bitpool = 2;
  result.cie.sbc_info.max_bitpool = 53;
  return result;
}

int32_t pcmDataCallback(uint8_t *buffer, int32_t length)
{
  if (length < 0)
  {
    pcmRequestCount = 0;
    lastPcmRequestLength = length;
    return 0;
  }
  memset(buffer, 0, static_cast<size_t>(length));
  lastPcmRequestLength = length;
  ++pcmRequestCount;
  pcmRequestObserved = true;
  return length;
}

void a2dpCallback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *parameter)
{
  if (parameter == nullptr) return;
  if (event == ESP_A2D_SEP_REG_STATE_EVT)
  {
    Serial.printf("A2DP_RAW_READY state=%u address=%s\n",
      static_cast<unsigned>(parameter->a2d_sep_reg_stat.reg_state),
      localAddress().c_str());
  }
  else if (event == ESP_A2D_CONNECTION_STATE_EVT)
  {
    if (parameter->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED)
    {
      connectionHandle = parameter->conn_stat.conn_hdl;
      Serial.printf("A2DP_RAW_CONNECTED mtu=%u\n",
        parameter->conn_stat.audio_mtu);
      esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
    }
    else if (parameter->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
    {
      connectionHandle = 0;
      Serial.println("A2DP_RAW_DISCONNECTED");
    }
  }
  else if (event == ESP_A2D_MEDIA_CTRL_ACK_EVT)
  {
    if (parameter->media_ctrl_stat.status != ESP_A2D_MEDIA_CTRL_ACK_SUCCESS)
    {
      Serial.printf("A2DP_RAW_MEDIA_FAILED cmd=%u status=%u\n",
        static_cast<unsigned>(parameter->media_ctrl_stat.cmd),
        static_cast<unsigned>(parameter->media_ctrl_stat.status));
    }
    else if (parameter->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY)
    {
      esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
    }
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
    sinkAddress[index] = static_cast<uint8_t>(value);
  }
  return true;
}

void initializeSource()
{
  const bool controller = btStartMode(BT_MODE_CLASSIC_BT);
  const esp_err_t bluedroidInit = controller
    ? esp_bluedroid_init() : ESP_ERR_INVALID_STATE;
  const esp_err_t bluedroidEnable = bluedroidInit == ESP_OK
    ? esp_bluedroid_enable() : ESP_ERR_INVALID_STATE;
  const esp_err_t callback = bluedroidEnable == ESP_OK
    ? esp_bt_gap_register_callback(gapCallback) : ESP_ERR_INVALID_STATE;
  esp_bt_io_cap_t capability = ESP_BT_IO_CAP_NONE;
  const esp_err_t security = callback == ESP_OK
    ? esp_bt_gap_set_security_param(
        ESP_BT_SP_IOCAP_MODE, &capability, sizeof(capability))
    : ESP_ERR_INVALID_STATE;
  const esp_err_t a2dpCallbackStatus = security == ESP_OK
    ? esp_a2d_register_callback(a2dpCallback) : ESP_ERR_INVALID_STATE;
  const esp_err_t dataCallbackStatus = a2dpCallbackStatus == ESP_OK
    ? esp_a2d_source_register_data_callback(pcmDataCallback)
    : ESP_ERR_INVALID_STATE;
  const esp_err_t source = dataCallbackStatus == ESP_OK
    ? esp_a2d_source_init() : ESP_ERR_INVALID_STATE;
  const esp_a2d_mcc_t endpoint = sbcEndpoint();
  const esp_err_t endpointStatus = source == ESP_OK
    ? esp_a2d_source_register_stream_endpoint(0, &endpoint)
    : ESP_ERR_INVALID_STATE;
  Serial.printf(
    "A2DP_RAW_INIT controller=%u bluedroid_init=%d "
    "bluedroid_enable=%d gap=%d security=%d callback=%d data=%d source=%d endpoint=%d\n",
    controller ? 1 : 0, static_cast<int>(bluedroidInit),
    static_cast<int>(bluedroidEnable), static_cast<int>(callback),
    static_cast<int>(security), static_cast<int>(a2dpCallbackStatus),
    static_cast<int>(dataCallbackStatus),
    static_cast<int>(source), static_cast<int>(endpointStatus));
  if (endpointStatus == ESP_OK)
  {
    Serial.printf("A2DP_RAW_READY state=0 address=%s\n",
      localAddress().c_str());
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);
}

void loop()
{
  static uint32_t reportedPcmRequests = 0;
  if (pcmRequestObserved && reportedPcmRequests == 0)
  {
    reportedPcmRequests = pcmRequestCount;
    Serial.printf("A2DP_RAW_PCM_REQUEST length=%d\n",
      static_cast<int>(lastPcmRequestLength));
  }
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == 'i') initializeSource();
    else if (command == 'c')
    {
      const String address = Serial.readStringUntil('\n');
      if (!parseAddress(address) ||
          esp_a2d_source_connect(sinkAddress) != ESP_OK)
        Serial.println("A2DP_RAW_CONNECT_FAILED");
    }
  }
  delay(1);
}
