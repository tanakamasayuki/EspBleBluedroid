// en: SppSerialServer - bridge an incoming SPP session and the board Serial port
//     with EspBluedroidSppSerial, the Arduino Stream view of SPP. The wrapper
//     follows the current active session by itself, so no session ID is stored.
//     It must never outlive the EspBleBluedroid instance it was built from.
// ja: SppSerialServer - 着信SPP sessionとボードのSerialを、SPPをArduino Streamとして
//     見せる EspBluedroidSppSerial で橋渡しする。wrapperは現在のactive sessionへ自分で
//     追従するため、session IDを保持する必要がない。構築元の EspBleBluedroid インスタンス
//     より長く生存させてはならない。
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
