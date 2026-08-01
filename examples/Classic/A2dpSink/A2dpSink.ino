#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Audio Sink";
  if (!bluetooth.begin(config))
  {
    Serial.printf("Bluetooth error: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  auto &sink = bluetooth.classic().a2dpSink();
  sink.onConnected([](const EspBluedroidA2dpSession &session) {
    Serial.printf("Connected: %s, session=%u\n",
      session.peerAddress.c_str(), static_cast<unsigned>(session.id));
  });
  sink.onPcmData([](const EspBluedroidA2dpPcmData &pcm) {
    // This runs on the A2DP stack task. Copy pcm.data to a bounded audio
    // queue here; the pointer becomes invalid when this callback returns.
    (void)pcm;
  });
  sink.onDisconnected([](const EspBluedroidA2dpSession &session) {
    Serial.printf("Disconnected: session=%u\n",
      static_cast<unsigned>(session.id));
  });

  if (!sink.start())
    Serial.printf("A2DP error: %s\n", bluetooth.lastErrorDetail().c_str());
}

void loop()
{
  bluetooth.update();
  delay(1);
}
