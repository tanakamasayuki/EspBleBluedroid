#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;
EspBluedroidSppSerial sppSerial(bluetooth);

void setup()
{
  Serial.begin(115200);
  delay(1500);

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Serial Server";
  if (!bluetooth.begin(config))
  {
    Serial.printf("begin failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.classic().spp().onConnected(
    [](const EspBluedroidSppSession &session) {
      Serial.printf("connected: id=%u peer=%s\n",
        static_cast<unsigned>(session.id), session.peerAddress.c_str());
    });
  bluetooth.classic().spp().onDisconnected(
    [](const EspBluedroidSppSession &session) {
      Serial.printf("disconnected: id=%u\n",
        static_cast<unsigned>(session.id));
    });

  EspBluedroidSppServerConfig server;
  server.serviceName = "EspBleBluedroid Serial";
  if (!bluetooth.classic().spp().startServer(server))
  {
    Serial.printf("startServer failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
  }
}

void loop()
{
  bluetooth.update();

  while (sppSerial.available() > 0)
  {
    Serial.write(sppSerial.read());
  }
  while (Serial.available() > 0 && sppSerial.connected())
  {
    sppSerial.write(Serial.read());
  }
  delay(1);
}
