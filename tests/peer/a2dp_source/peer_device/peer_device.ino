#include <Arduino.h>
#include <esp_a2dp_api.h>
#include <esp_avrc_api.h>
#include <esp_bt_device.h>
#include <esp_gap_bt_api.h>
#include <esp_bt_main.h>
#include <esp32-hal-alloc-bt-classic-mem.h>
#include <esp32-hal-bt.h>

volatile uint32_t pcmBytes = 0;
volatile bool pcmObserved = false;
volatile bool avrcpConnected = false;

void avrcpControllerCallback(
  esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *parameter)
{
  if (parameter == nullptr) return;
  if (event == ESP_AVRC_CT_CONNECTION_STATE_EVT && parameter->conn_stat.connected)
  {
    avrcpConnected = true;
    Serial.println("AVRCP_RAW_CT_CONNECTED");
  }
}

void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *parameter)
{
  if (parameter != nullptr && event == ESP_BT_GAP_CFM_REQ_EVT)
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

void pcmDataCallback(const uint8_t *buffer, uint32_t length)
{
  bool allZero = length != 0;
  for (uint32_t index = 0; index < length; ++index)
    allZero = allZero && buffer[index] == 0;
  if (allZero)
  {
    pcmBytes = length;
    pcmObserved = true;
  }
}

void a2dpCallback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *parameter)
{
  if (parameter == nullptr) return;
  if (event == ESP_A2D_CONNECTION_STATE_EVT)
  {
    if (parameter->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED)
      Serial.printf("A2DP_RAW_SINK_CONNECTED mtu=%u\n",
        parameter->conn_stat.audio_mtu);
    else if (parameter->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
      Serial.println("A2DP_RAW_SINK_DISCONNECTED");
  }
}

void initializeSink()
{
  const bool controller = btStartMode(BT_MODE_CLASSIC_BT);
  const esp_err_t bluedroidInit = controller
    ? esp_bluedroid_init() : ESP_ERR_INVALID_STATE;
  const esp_err_t bluedroidEnable = bluedroidInit == ESP_OK
    ? esp_bluedroid_enable() : ESP_ERR_INVALID_STATE;
  const esp_err_t gap = bluedroidEnable == ESP_OK
    ? esp_bt_gap_register_callback(gapCallback) : ESP_ERR_INVALID_STATE;
  esp_bt_io_cap_t capability = ESP_BT_IO_CAP_NONE;
  const esp_err_t security = gap == ESP_OK
    ? esp_bt_gap_set_security_param(
        ESP_BT_SP_IOCAP_MODE, &capability, sizeof(capability))
    : ESP_ERR_INVALID_STATE;
  const esp_err_t callback = security == ESP_OK
    && esp_avrc_ct_init() == ESP_OK &&
      esp_avrc_ct_register_callback(avrcpControllerCallback) == ESP_OK
    ? esp_a2d_register_callback(a2dpCallback) : ESP_ERR_INVALID_STATE;
  const esp_err_t data = callback == ESP_OK
    ? esp_a2d_sink_register_data_callback(pcmDataCallback)
    : ESP_ERR_INVALID_STATE;
  const esp_err_t sink = data == ESP_OK
    ? esp_a2d_sink_init() : ESP_ERR_INVALID_STATE;
  const esp_a2d_mcc_t endpoint = sbcEndpoint();
  const esp_err_t endpointStatus = sink == ESP_OK
    ? esp_a2d_sink_register_stream_endpoint(0, &endpoint)
    : ESP_ERR_INVALID_STATE;
  const esp_err_t scan = endpointStatus == ESP_OK
    ? esp_bt_gap_set_scan_mode(
        ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE)
    : ESP_ERR_INVALID_STATE;
  Serial.printf(
    "A2DP_RAW_SINK_INIT controller=%u bluedroid_init=%d enable=%d gap=%d "
    "security=%d callback=%d data=%d sink=%d endpoint=%d scan=%d\n",
    controller ? 1 : 0, static_cast<int>(bluedroidInit),
    static_cast<int>(bluedroidEnable), static_cast<int>(gap),
    static_cast<int>(security), static_cast<int>(callback),
    static_cast<int>(data), static_cast<int>(sink),
    static_cast<int>(endpointStatus), static_cast<int>(scan));
  if (scan == ESP_OK)
    Serial.printf("A2DP_RAW_SINK_READY address=%s\n", localAddress().c_str());
}

void setup()
{
  Serial.begin(115200);
  delay(500);
}

void loop()
{
  static bool reported = false;
  if (pcmObserved && !reported)
  {
    Serial.printf("A2DP_RAW_SINK_PCM length=%u zero=1\n",
      static_cast<unsigned>(pcmBytes));
    reported = true;
  }
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == 'i') initializeSink();
    else if (command == 'p' && avrcpConnected)
    {
      esp_avrc_ct_send_passthrough_cmd(
        1, ESP_AVRC_PT_CMD_PAUSE, ESP_AVRC_PT_CMD_STATE_PRESSED);
      esp_avrc_ct_send_passthrough_cmd(
        2, ESP_AVRC_PT_CMD_PAUSE, ESP_AVRC_PT_CMD_STATE_RELEASED);
    }
    else if (command == 'v' && avrcpConnected)
      esp_avrc_ct_send_set_absolute_volume_cmd(3, 91);
  }
  delay(1);
}
