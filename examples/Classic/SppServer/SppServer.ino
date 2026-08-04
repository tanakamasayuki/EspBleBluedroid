// en: SppServer - start an unauthenticated Serial Port Profile server and echo
//     every received packet. SPP is the Classic byte-stream profile: RFCOMM in,
//     RFCOMM out, no GATT database. Callbacks are delivered from update(), so
//     echoing from inside onData() only queues the write.
// ja: SppServer - 認証なしのSerial Port Profile serverを開始し、受信packetをすべて
//     echoする。SPPはClassicのbyte stream profileで、RFCOMMの入出力だけを扱い、GATT
//     databaseはない。callbackは update() から配送されるため、onData() の中のechoは
//     writeをqueueへ入れるだけになる。
#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);
  if (!bluetooth.capabilities().classicSpp)
  {
    Serial.println("Classic SPP is unavailable on this target");
    return;
  }
  if (!bluetooth.begin())
  {
    Serial.printf("begin failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.classic().spp().onServerStarted([]() {
    Serial.println("SPP server started");
  });
  bluetooth.classic().spp().onConnected(
    [](const EspBluedroidSppSession &session) {
      Serial.printf("connected: id=%u peer=%s\n",
        static_cast<unsigned>(session.id), session.peerAddress.c_str());
    });
  bluetooth.classic().spp().onData([](const EspBluedroidSppData &event) {
    Serial.printf("received %u bytes\n",
      static_cast<unsigned>(event.value.length()));
    if (!bluetooth.classic().spp().write(event.sessionId, event.value))
    {
      Serial.printf("echo failed: %s\n", bluetooth.lastErrorName());
    }
  });
  bluetooth.classic().spp().onDisconnected(
    [](const EspBluedroidSppSession &session) {
      Serial.printf("disconnected: id=%u\n",
        static_cast<unsigned>(session.id));
    });

  EspBluedroidSppServerConfig config;
  config.serviceName = "EspBleBluedroid SPP";
  if (!bluetooth.classic().spp().startServer(config))
  {
    Serial.printf("startServer failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
  }
}

void loop()
{
  bluetooth.update();
  delay(1);
}
