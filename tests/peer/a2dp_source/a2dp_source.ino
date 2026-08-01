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
  const bool prebegin = bluetooth.classic().a2dpSource().start();
  Serial.printf("A2DP_SOURCE_PREBEGIN_REJECTED %u error=%s\n",
    prebegin ? 0 : 1, bluetooth.lastErrorName());

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid A2DP Source";
  if (!bluetooth.begin(config))
  {
    Serial.printf("A2DP_SOURCE_INIT_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  auto &source = bluetooth.classic().a2dpSource();
  source.onStarted([](const EspBluedroidA2dpStartResult &result) {
    Serial.printf("A2DP_SOURCE_STARTED success=%u address=%s context=%s\n",
      result.success ? 1 : 0, localClassicAddress().c_str(), contextName());
  });
  source.onConnected([](const EspBluedroidA2dpSession &session) {
    sessionId = session.id;
    Serial.printf(
      "A2DP_SOURCE_CONNECTED id=%u address=%s incoming=%u mtu=%u context=%s\n",
      static_cast<unsigned>(session.id), session.peerAddress.c_str(),
      session.incoming ? 1 : 0, session.audioMtu, contextName());
    Serial.printf("A2DP_SOURCE_STREAM_REQUEST %u\n",
      bluetooth.classic().a2dpSource().startStream() ? 1 : 0);
  });
  source.onPcmRequested([](EspBluedroidA2dpPcmRequest &request) {
    static bool reported = false;
    if (request.flush) return;
    memset(request.data, 0, request.capacity);
    request.written = request.capacity;
    if (!reported)
    {
      Serial.printf(
        "A2DP_SOURCE_PCM id=%u capacity=%u rate=%u channels=%u bits=%u "
        "context=%s\n",
        static_cast<unsigned>(request.sessionId),
        static_cast<unsigned>(request.capacity),
        static_cast<unsigned>(request.format.sampleRate),
        request.format.channels, request.format.bitsPerSample,
        contextName());
      reported = true;
    }
  });
  source.onStreamChanged([](const EspBluedroidA2dpStreamChanged &event) {
    Serial.printf("A2DP_SOURCE_STREAM id=%u state=%u context=%s\n",
      static_cast<unsigned>(event.sessionId),
      static_cast<unsigned>(event.state), contextName());
  });
  source.onDisconnected([](const EspBluedroidA2dpSession &session) {
    Serial.printf("A2DP_SOURCE_DISCONNECTED id=%u context=%s\n",
      static_cast<unsigned>(session.id), contextName());
    sessionId = 0;
  });

  Serial.printf("A2DP_SOURCE_START_ACCEPTED %u\n", source.start() ? 1 : 0);
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
    else if (command == 'c')
    {
      const String address = Serial.readStringUntil('\n');
      Serial.printf("A2DP_SOURCE_CONNECT_ACCEPTED %u\n",
        bluetooth.classic().a2dpSource().connect(address.c_str()) ? 1 : 0);
    }
    else if (command == 'd' && sessionId != 0)
      bluetooth.classic().a2dpSource().disconnect(sessionId);
    else if (command == 'e')
    {
      bluetooth.end();
      Serial.printf("A2DP_SOURCE_END initialized=%u\n",
        bluetooth.initialized() ? 1 : 0);
    }
  }
  bluetooth.update();
  delay(1);
}
