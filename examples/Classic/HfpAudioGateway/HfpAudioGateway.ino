#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Audio Gateway";
  if (!bluetooth.begin(config)) return;

  auto &gateway = bluetooth.classic().hfpAudioGateway();
  gateway.onConnected([](const EspBluedroidHfpSession &session) {
    Serial.printf("HF connected: %s\n", session.peerAddress.c_str());
  });
  gateway.onPcmData([](const EspBluedroidHfpPcmData &pcm) {
    // Copy microphone PCM received from the Hands-Free device.
    (void)pcm;
  });
  gateway.onPcmRequested([](EspBluedroidHfpPcmRequest &request) {
    // Replace silence with downlink call audio.
    memset(request.data, 0, request.capacity);
    request.written = request.capacity;
  });
  gateway.start();
}

void loop()
{
  bluetooth.update();
  delay(1);
}
