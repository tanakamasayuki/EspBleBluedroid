#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);
  if (!bluetooth.begin())
  {
    Serial.printf("begin failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.classic().spp().onConnected(
    [](const EspBluedroidSppSession &session) {
      Serial.printf("connected: id=%u peer=%s\n",
        static_cast<unsigned>(session.id), session.peerAddress.c_str());
      bluetooth.classic().spp().write(session.id, String("hello"));
    });
  bluetooth.classic().spp().onData([](const EspBluedroidSppData &event) {
    Serial.printf("received %u bytes on session %u\n",
      static_cast<unsigned>(event.value.length()),
      static_cast<unsigned>(event.sessionId));
  });
  bluetooth.classic().spp().onDisconnected(
    [](const EspBluedroidSppSession &session) {
      Serial.printf("disconnected: id=%u\n",
        static_cast<unsigned>(session.id));
    });
  bluetooth.classic().spp().onConnectionFailed(
    [](const EspBluedroidSppConnectionFailure &failure) {
      Serial.printf("connect failed: %s (%s)\n",
        failure.peerAddress.c_str(), failure.detail.c_str());
    });

  Serial.println("Enter a Classic address such as 01:23:45:67:89:ab");
}

void loop()
{
  if (Serial.available())
  {
    const String address = Serial.readStringUntil('\n');
    if (!bluetooth.classic().spp().connect(address.c_str()))
    {
      Serial.printf("request rejected: %s (%s)\n",
        bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    }
  }
  bluetooth.update();
  delay(1);
}
