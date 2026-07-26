#include <EspBleBluedroid.h>
#include <esp_bt_device.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
bool initialized = false;
size_t eventBytes = 0;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

String localAddress()
{
  const uint8_t *address = esp_bt_dev_get_address();
  char value[18];
  snprintf(value, sizeof(value), "%02x:%02x:%02x:%02x:%02x:%02x",
    address[0], address[1], address[2], address[3], address[4], address[5]);
  return String(value);
}

void initializeBluetooth()
{
  if (!bluetooth.begin())
  {
    Serial.printf("SPP_RX_INIT_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  bluetooth.classic().spp().onServerStarted([]() {
    Serial.printf("SPP_RX_SERVER_READY address=%s capacity=%u\n",
      localAddress().c_str(),
      static_cast<unsigned>(EspBluedroidSpp::ReceiveBufferCapacity));
  });
  bluetooth.classic().spp().onData([](const EspBluedroidSppData &event) {
    eventBytes += event.value.length();
    if (eventBytes < 2300) return;

    const size_t availableBefore =
      bluetooth.classic().spp().available(event.sessionId);
    const int first = bluetooth.classic().spp().peek(event.sessionId);
    static uint8_t data[EspBluedroidSpp::ReceiveBufferCapacity];
    const size_t read = bluetooth.classic().spp().read(
      event.sessionId, data, sizeof(data));
    uint32_t checksum = 0;
    for (size_t index = 0; index < read; ++index) checksum += data[index];
    Serial.printf(
      "SPP_RX_BUFFER event_bytes=%u available=%u dropped=%u "
      "peek=%d read=%u remaining=%u checksum=%u context=%s\n",
      static_cast<unsigned>(eventBytes),
      static_cast<unsigned>(availableBefore),
      static_cast<unsigned>(
        bluetooth.classic().spp().droppedReceiveByteCount()),
      first, static_cast<unsigned>(read),
      static_cast<unsigned>(
        bluetooth.classic().spp().available(event.sessionId)),
      static_cast<unsigned>(checksum), contextName());
    Serial.printf("SPP_RX_EMPTY_READ %d\n",
      bluetooth.classic().spp().read(event.sessionId));
    bluetooth.classic().spp().write(event.sessionId, String("done"));
  });
  bluetooth.classic().spp().onDisconnected(
    [](const EspBluedroidSppSession &) {
      Serial.printf("SPP_RX_DISCONNECTED available=%u\n",
        static_cast<unsigned>(
          bluetooth.classic().spp().available(1)));
    });

  EspBluedroidSppServerConfig config;
  config.serviceName = "EspBleBluedroid RX";
  if (!bluetooth.classic().spp().startServer(config))
  {
    Serial.printf("SPP_RX_SERVER_FAILED %s\n", bluetooth.lastErrorName());
  }
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
  }
  bluetooth.update();
  delay(1);
}
