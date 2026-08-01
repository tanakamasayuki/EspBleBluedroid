#include <EspBleBluedroid.h>
#include <esp_bt_device.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
EspBluedroidA2dpSessionId sessionId = 0;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

String localClassicAddress()
{
  const uint8_t *address = esp_bt_dev_get_address();
  if (address == nullptr) return "";
  char value[18];
  snprintf(value, sizeof(value), "%02x:%02x:%02x:%02x:%02x:%02x",
    address[0], address[1], address[2], address[3], address[4], address[5]);
  return String(value);
}

void initializeBluetooth()
{
  const bool prebegin = bluetooth.classic().a2dpSink().start();
  Serial.printf("A2DP_SINK_PREBEGIN_REJECTED %u error=%s\n",
    prebegin ? 0 : 1, bluetooth.lastErrorName());

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid A2DP Sink";
  if (!bluetooth.begin(config))
  {
    Serial.printf("A2DP_SINK_INIT_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  auto &sink = bluetooth.classic().a2dpSink();
  sink.onStarted([](const EspBluedroidA2dpStartResult &result) {
    Serial.printf("A2DP_SINK_STARTED success=%u address=%s context=%s\n",
      result.success ? 1 : 0, localClassicAddress().c_str(), contextName());
  });
  sink.onConnected([](const EspBluedroidA2dpSession &session) {
    sessionId = session.id;
    Serial.printf(
      "A2DP_SINK_CONNECTED id=%u address=%s incoming=%u mtu=%u context=%s\n",
      static_cast<unsigned>(session.id), session.peerAddress.c_str(),
      session.incoming ? 1 : 0, session.audioMtu, contextName());
  });
  sink.onStreamChanged([](const EspBluedroidA2dpStreamChanged &event) {
    Serial.printf("A2DP_SINK_STREAM id=%u state=%u context=%s\n",
      static_cast<unsigned>(event.sessionId),
      static_cast<unsigned>(event.state), contextName());
  });
  sink.onPcmData([](const EspBluedroidA2dpPcmData &pcm) {
    static bool reported = false;
    if (reported) return;
    bool allZero = pcm.length != 0;
    for (size_t index = 0; index < pcm.length; ++index)
      allZero = allZero && pcm.data[index] == 0;
    Serial.printf(
      "A2DP_SINK_PCM id=%u length=%u rate=%u channels=%u bits=%u "
      "zero=%u context=%s\n",
      static_cast<unsigned>(pcm.sessionId),
      static_cast<unsigned>(pcm.length),
      static_cast<unsigned>(pcm.format.sampleRate),
      pcm.format.channels, pcm.format.bitsPerSample,
      allZero ? 1 : 0, contextName());
    reported = true;
  });
  sink.onDisconnected([](const EspBluedroidA2dpSession &session) {
    Serial.printf("A2DP_SINK_DISCONNECTED id=%u context=%s\n",
      static_cast<unsigned>(session.id), contextName());
    sessionId = 0;
  });

  Serial.printf("A2DP_SINK_START_ACCEPTED %u\n", sink.start() ? 1 : 0);
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();
}

void loop()
{
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == 'i') initializeBluetooth();
    else if (command == 'd' && sessionId != 0)
      bluetooth.classic().a2dpSink().disconnect(sessionId);
    else if (command == 'e')
    {
      bluetooth.end();
      Serial.printf("A2DP_SINK_END initialized=%u\n",
        bluetooth.initialized() ? 1 : 0);
    }
  }
  bluetooth.update();
  delay(1);
}
