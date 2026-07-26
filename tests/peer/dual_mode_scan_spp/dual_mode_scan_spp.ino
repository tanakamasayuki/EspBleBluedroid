#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
EspBluedroidSppSessionId sppSessionId = 0;
bool initialized = false;
bool scanResultHandled = false;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void initializeBluetooth()
{
  if (!bluetooth.begin())
  {
    Serial.printf("DUAL_INIT_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (scanResultHandled || result.name != "Bluedroid Dual Peer") return;
    scanResultHandled = true;
    const bool stopped = bluetooth.scanner().stop();
    Serial.printf(
      "DUAL_BLE_SCAN_FOUND name=%s spp_sessions=%u stopped=%u context=%s\n",
      result.name.c_str(),
      static_cast<unsigned>(bluetooth.classic().spp().sessionCount()),
      stopped ? 1 : 0, contextName());
    const uint8_t message[] = {0xd0, 0x00, 'H'};
    Serial.printf("DUAL_SPP_WRITE_ACCEPTED %u\n",
      bluetooth.classic().spp().write(
        sppSessionId, message, sizeof(message)) ? 1 : 0);
  });
  bluetooth.classic().spp().onConnected(
    [](const EspBluedroidSppSession &session) {
      sppSessionId = session.id;
      EspBleScanConfig scanConfig;
      scanConfig.active = true;
      scanConfig.durationSeconds = 10;
      Serial.printf(
        "DUAL_SPP_CONNECTED id=%u scan_started=%u context=%s\n",
        static_cast<unsigned>(session.id),
        bluetooth.scanner().start(scanConfig) ? 1 : 0,
        contextName());
    });
  bluetooth.classic().spp().onData([](const EspBluedroidSppData &event) {
    Serial.printf("DUAL_SPP_RX id=%u length=%u hex=",
      static_cast<unsigned>(event.sessionId),
      static_cast<unsigned>(event.value.length()));
    for (size_t index = 0; index < event.value.length(); ++index)
    {
      Serial.printf("%02x", static_cast<uint8_t>(event.value[index]));
    }
    Serial.printf(" scan=%u context=%s\n",
      bluetooth.scanner().isScanning() ? 1 : 0, contextName());
    bluetooth.classic().spp().disconnect(event.sessionId);
  });
  bluetooth.classic().spp().onDisconnected(
    [](const EspBluedroidSppSession &session) {
      Serial.printf("DUAL_COMPLETE id=%u sessions=%u context=%s\n",
        static_cast<unsigned>(session.id),
        static_cast<unsigned>(bluetooth.classic().spp().sessionCount()),
        contextName());
    });
  bluetooth.classic().spp().onConnectionFailed(
    [](const EspBluedroidSppConnectionFailure &failure) {
      Serial.printf("DUAL_CONNECT_FAILED %s %s\n",
        failure.peerAddress.c_str(), failure.detail.c_str());
    });
  Serial.println("DUAL_HOST_READY");
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
    else if (command == 'c' && bluetooth.initialized())
    {
      const String address = Serial.readStringUntil('\n');
      Serial.printf("DUAL_CONNECT_ACCEPTED %u\n",
        bluetooth.classic().spp().connect(address.c_str()) ? 1 : 0);
    }
  }
  bluetooth.update();
  delay(1);
}
