#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

EspBleBluedroid bluetooth;
EspBluedroidSppSerial sppSerial(bluetooth);
TaskHandle_t loopTask = nullptr;
bool initialized = false;
EspBluedroidSppSessionId activeSessionId = 0;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void initializeBluetooth()
{
  const bool prebeginAccepted =
    bluetooth.classic().spp().connect("00:11:22:33:44:55");
  Serial.printf("SPP_CLIENT_PREBEGIN_REJECTED %u error=%s\n",
    prebeginAccepted ? 0 : 1, bluetooth.lastErrorName());

  if (!bluetooth.begin())
  {
    Serial.printf("SPP_CLIENT_INIT_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }
  bluetooth.classic().spp().onConnected(
    [](const EspBluedroidSppSession &session) {
      activeSessionId = session.id;
      Serial.printf(
        "SPP_CLIENT_CONNECTED id=%u address=%s incoming=%u "
        "stream=%u stream_id=%u context=%s\n",
        static_cast<unsigned>(session.id), session.peerAddress.c_str(),
        session.incoming ? 1 : 0, sppSerial.connected() ? 1 : 0,
        static_cast<unsigned>(sppSerial.sessionId()), contextName());
      const uint8_t reply[] = {0xfe, 0x00, 'C'};
      Serial.printf("SPP_CLIENT_WRITE_ACCEPTED %u\n",
        sppSerial.write(reply, sizeof(reply)) == sizeof(reply) ? 1 : 0);
    });
  bluetooth.classic().spp().onData([](const EspBluedroidSppData &event) {
    Serial.printf("SPP_CLIENT_RX id=%u length=%u hex=",
      static_cast<unsigned>(event.sessionId),
      static_cast<unsigned>(event.value.length()));
    for (size_t index = 0; index < event.value.length(); ++index)
    {
      Serial.printf("%02x", static_cast<uint8_t>(event.value[index]));
    }
    Serial.printf(" context=%s\n", contextName());
  });
  bluetooth.classic().spp().onWriteCompleted(
    [](const EspBluedroidSppWriteResult &result) {
      Serial.printf(
        "SPP_CLIENT_WRITE_COMPLETED id=%u length=%u success=%u "
        "error=%u context=%s\n",
        static_cast<unsigned>(result.sessionId),
        static_cast<unsigned>(result.length),
        result.success ? 1 : 0,
        static_cast<unsigned>(result.error), contextName());
    });
  bluetooth.classic().spp().onDisconnected(
    [](const EspBluedroidSppSession &session) {
      activeSessionId = 0;
      Serial.printf(
        "SPP_CLIENT_DISCONNECTED id=%u remaining=%u "
        "stream=%u stream_id=%u context=%s\n",
        static_cast<unsigned>(session.id),
        static_cast<unsigned>(bluetooth.classic().spp().sessionCount()),
        sppSerial.connected() ? 1 : 0,
        static_cast<unsigned>(sppSerial.sessionId()), contextName());
    });
  bluetooth.classic().spp().onConnectionFailed(
    [](const EspBluedroidSppConnectionFailure &failure) {
      Serial.printf("SPP_CLIENT_FAILED address=%s error=%u context=%s\n",
        failure.peerAddress.c_str(),
        static_cast<unsigned>(failure.error), contextName());
    });
  Serial.println("SPP_PUBLIC_CLIENT_READY");
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
      const uint32_t startedAt = millis();
      const bool accepted = bluetooth.classic().spp().connect(address.c_str());
      Serial.printf("SPP_CLIENT_CONNECT_ACCEPTED %u elapsed=%u\n",
        accepted ? 1 : 0,
        static_cast<unsigned>(millis() - startedAt));
    }
    else if (command == 'd' && activeSessionId != 0)
    {
      Serial.printf("SPP_CLIENT_DISCONNECT_ACCEPTED %u\n",
        bluetooth.classic().spp().disconnect(activeSessionId) ? 1 : 0);
    }
    else if (command == 'f' && bluetooth.initialized())
    {
      const String address = Serial.readStringUntil('\n');
      const uint32_t startedAt = millis();
      const bool accepted =
        bluetooth.classic().spp().connect(address.c_str(), 2000);
      Serial.printf("SPP_CLIENT_FAILURE_ACCEPTED %u elapsed=%u\n",
        accepted ? 1 : 0,
        static_cast<unsigned>(millis() - startedAt));
    }
  }
  bluetooth.update();
  delay(1);
}
