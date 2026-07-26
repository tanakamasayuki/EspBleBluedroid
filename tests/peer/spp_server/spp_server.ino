#include <EspBleBluedroid.h>
#include <esp_bt_device.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
EspBluedroidSppSessionId sessionId = 0;
bool initialized = false;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
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

void initializeBluetooth()
{
  const EspBluedroidCapabilities capabilities = bluetooth.capabilities();
  Serial.printf("SPP_CAPABILITIES classic=%u spp=%u\n",
    capabilities.classic ? 1 : 0, capabilities.classicSpp ? 1 : 0);

  EspBluedroidSppServerConfig serverConfig;
  serverConfig.serviceName = "EspBleBluedroid SPP";
  const bool prebeginAccepted =
    bluetooth.classic().spp().startServer(serverConfig);
  Serial.printf("SPP_PREBEGIN_REJECTED %u error=%s\n",
    prebeginAccepted ? 0 : 1, bluetooth.lastErrorName());

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid SPP Server";
  if (!bluetooth.begin(config))
  {
    Serial.printf("SPP_INIT_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.classic().spp().onServerStarted([]() {
    Serial.printf("SPP_SERVER_STARTED address=%s running=%u context=%s\n",
      localAddress().c_str(),
      bluetooth.classic().spp().serverRunning() ? 1 : 0,
      contextName());
  });
  bluetooth.classic().spp().onConnected(
    [](const EspBluedroidSppSession &session) {
      sessionId = session.id;
      Serial.printf(
        "SPP_SERVER_CONNECTED id=%u address=%s incoming=%u context=%s\n",
        static_cast<unsigned>(session.id), session.peerAddress.c_str(),
        session.incoming ? 1 : 0, contextName());
    });
  bluetooth.classic().spp().onData([](const EspBluedroidSppData &event) {
    Serial.printf("SPP_SERVER_RX id=%u length=%u hex=",
      static_cast<unsigned>(event.sessionId),
      static_cast<unsigned>(event.value.length()));
    for (size_t index = 0; index < event.value.length(); ++index)
    {
      Serial.printf("%02x", static_cast<uint8_t>(event.value[index]));
    }
    Serial.printf(" context=%s\n", contextName());
    const uint8_t reply[] = {0xff, 0x00, 'S'};
    Serial.printf("SPP_SERVER_WRITE_ACCEPTED %u\n",
      bluetooth.classic().spp().write(
        event.sessionId, reply, sizeof(reply)) ? 1 : 0);
  });
  bluetooth.classic().spp().onDisconnected(
    [](const EspBluedroidSppSession &session) {
      Serial.printf("SPP_SERVER_DISCONNECTED id=%u remaining=%u context=%s\n",
        static_cast<unsigned>(session.id),
        static_cast<unsigned>(bluetooth.classic().spp().sessionCount()),
        contextName());
    });

  Serial.printf("SPP_SERVER_START_ACCEPTED %u\n",
    bluetooth.classic().spp().startServer(serverConfig) ? 1 : 0);
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
    if (command == 'i' && !initialized)
    {
      initialized = true;
      initializeBluetooth();
    }
    else if (command == 'e' && bluetooth.initialized())
    {
      const uint32_t startedAt = millis();
      bluetooth.end();
      Serial.printf("SPP_END_DONE initialized=%u elapsed=%u\n",
        bluetooth.initialized() ? 1 : 0,
        static_cast<unsigned>(millis() - startedAt));
    }
  }
  bluetooth.update();
  delay(1);
}
