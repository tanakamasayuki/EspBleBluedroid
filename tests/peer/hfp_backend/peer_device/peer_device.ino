#include <Arduino.h>
#include <EspBleBluedroid.h>
#include <esp_bt_device.h>

EspBleBluedroid bluetooth;

String localClassicAddress()
{
  const uint8_t *address = esp_bt_dev_get_address();
  if (!address) return "";
  char text[18];
  snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x",
    address[0], address[1], address[2], address[3], address[4], address[5]);
  return String(text);
}

void initializeGateway()
{
  EspBleConfig config;
  config.deviceName = "EspBleBluedroid HFP AG";
  if (!bluetooth.begin(config)) return;
  auto &gateway = bluetooth.classic().hfpAudioGateway();
  gateway.onStarted([](const EspBluedroidHfpStartResult &result) {
    Serial.printf("HFP_AG_STARTED success=%u\n", result.success ? 1 : 0);
    if (result.success)
      Serial.printf("HFP_AG_READY address=%s\n", localClassicAddress().c_str());
  });
  gateway.onConnected([](const EspBluedroidHfpSession &) {
    Serial.println("HFP_AG_CONNECTION state=3");
  });
  gateway.onDisconnected([](const EspBluedroidHfpSession &) {
    Serial.println("HFP_AG_CONNECTION state=0");
  });
  gateway.onAudioChanged([](const EspBluedroidHfpAudioChanged &event) {
    Serial.printf("HFP_AG_AUDIO state=%u frame=0 handle=0 rate=%u\n",
      event.connected ? 3 : 0, static_cast<unsigned>(event.format.sampleRate));
  });
  gateway.onPcmData([](const EspBluedroidHfpPcmData &pcm) {
    static bool reported = false;
    if (!reported && pcm.length)
    {
      reported = true;
      Serial.printf("HFP_AG_PCM bytes=%u rate=%u\n",
        static_cast<unsigned>(pcm.length),
        static_cast<unsigned>(pcm.format.sampleRate));
    }
  });
  gateway.onPcmRequested([](EspBluedroidHfpPcmRequest &request) {
    memset(request.data, 0xa5, request.capacity);
    request.written = request.capacity;
  });
  Serial.printf("HFP_AG_START %u\n", gateway.start() ? 1 : 0);
}

void setup()
{
  Serial.begin(115200);
  delay(500);
}

void loop()
{
  if (Serial.available() && Serial.read() == 'i') initializeGateway();
  bluetooth.update();
  delay(1);
}
