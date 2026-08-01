#include <Arduino.h>
#include <EspBleBluedroid.h>
#include <esp_bt_device.h>

EspBleBluedroid bluetooth;
EspBluedroidHfpSessionId sessionId = 0;

String localClassicAddress()
{
  const uint8_t *address = esp_bt_dev_get_address();
  if (!address) return "";
  char text[18];
  snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x",
    address[0], address[1], address[2], address[3], address[4], address[5]);
  return String(text);
}

void initializeClient()
{
  EspBleConfig config;
  config.deviceName = "EspBleBluedroid HFP HF";
  if (!bluetooth.begin(config)) return;
  auto &handsFree = bluetooth.classic().hfpHandsFree();
  handsFree.onStarted([](const EspBluedroidHfpStartResult &result) {
    Serial.printf("HFP_HF_STARTED success=%u\n", result.success ? 1 : 0);
    if (result.success)
      Serial.printf("HFP_HF_READY address=%s\n", localClassicAddress().c_str());
  });
  handsFree.onConnected([](const EspBluedroidHfpSession &session) {
    sessionId = session.id;
    Serial.println("HFP_HF_CONNECTION state=3");
  });
  handsFree.onDisconnected([](const EspBluedroidHfpSession &) {
    sessionId = 0;
    Serial.println("HFP_HF_CONNECTION state=0");
  });
  handsFree.onAudioChanged([](const EspBluedroidHfpAudioChanged &event) {
    Serial.printf("HFP_HF_AUDIO state=%u frame=0 handle=0 rate=%u\n",
      event.connected ? 3 : 0, static_cast<unsigned>(event.format.sampleRate));
  });
  handsFree.onPcmData([](const EspBluedroidHfpPcmData &pcm) {
    static bool reported = false;
    if (!reported && pcm.length)
    {
      reported = true;
      Serial.printf("HFP_HF_PCM bytes=%u rate=%u\n",
        static_cast<unsigned>(pcm.length),
        static_cast<unsigned>(pcm.format.sampleRate));
    }
  });
  handsFree.onPcmRequested([](EspBluedroidHfpPcmRequest &request) {
    memset(request.data, 0x5a, request.capacity);
    request.written = request.capacity;
  });
  Serial.printf("HFP_HF_START %u\n", handsFree.start() ? 1 : 0);
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
      Serial.printf("HFP_HF_CONNECT %u\n",
        bluetooth.classic().hfpHandsFree().connect(address.c_str()) ? 1 : 0);
    }
    else if (command == 'a')
      Serial.printf("HFP_HF_AUDIO_CONNECT %u\n",
        bluetooth.classic().hfpHandsFree().connectAudio(sessionId) ? 1 : 0);
    else if (command == 'd')
      bluetooth.classic().hfpHandsFree().disconnect(sessionId);
  }
  bluetooth.update();
  delay(1);
}
