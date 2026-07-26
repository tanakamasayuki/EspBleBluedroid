#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
bool peerFound = false;
bool initialized = false;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void initializeBluetooth()
{
  const EspBluedroidCapabilities before = bluetooth.capabilities();
  Serial.printf(
    "CLASSIC_CAPABILITIES ble=%u classic=%u dual=%u inquiry=%u spp=%u\n",
    before.ble ? 1 : 0, before.classic ? 1 : 0,
    before.dualMode ? 1 : 0, before.classicInquiry ? 1 : 0,
    before.classicSpp ? 1 : 0);
  const bool prebeginAccepted = bluetooth.classic().inquiry().start();
  Serial.printf("CLASSIC_PREBEGIN_REJECTED %u error=%s\n",
    prebeginAccepted ? 0 : 1, bluetooth.lastErrorName());

  EspBleConfig config;
  config.deviceName = "Bluedroid Dual Central";
  if (!bluetooth.begin(config))
  {
    Serial.printf("CLASSIC_INIT_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.classic().inquiry().onResult(
    [](const EspBluedroidClassicInquiryResult &result) {
      if (peerFound || result.name != "Bluedroid Classic Peer") return;
      peerFound = true;
      Serial.printf(
        "CLASSIC_RESULT address=%s name=%s cod=%u rssi=%d has_cod=%u has_rssi=%u context=%s\n",
        result.address.c_str(), result.name.c_str(),
        static_cast<unsigned>(result.classOfDevice), result.rssi,
        result.hasClassOfDevice ? 1 : 0, result.hasRssi ? 1 : 0,
        contextName());
      Serial.printf("CLASSIC_STOP_ACCEPTED %u\n",
        bluetooth.classic().inquiry().stop() ? 1 : 0);
    });
  bluetooth.classic().inquiry().onComplete(
    [](const EspBluedroidClassicInquiryComplete &event) {
      Serial.printf(
        "CLASSIC_COMPLETE cancelled=%u running=%u context=%s\n",
        event.cancelled ? 1 : 0,
        bluetooth.classic().inquiry().isRunning() ? 1 : 0,
        contextName());
    });
  Serial.println("CLASSIC_CENTRAL_READY");
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
    else if (command == 's' && bluetooth.initialized())
    {
      EspBluedroidClassicInquiryConfig config;
      config.durationSeconds = 5;
      Serial.printf("CLASSIC_START_ACCEPTED %u\n",
        bluetooth.classic().inquiry().start(config) ? 1 : 0);
    }
  }
  bluetooth.update();
  delay(1);
}
