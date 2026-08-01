#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;
EspBluedroidHfpSessionId sessionId = 0;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Hands-Free";
  if (!bluetooth.begin(config)) return;

  auto &handsFree = bluetooth.classic().hfpHandsFree();
  handsFree.onConnected([](const EspBluedroidHfpSession &session) {
    sessionId = session.id;
    bluetooth.classic().hfpHandsFree().connectAudio(session.id);
  });
  handsFree.onAudioChanged([](const EspBluedroidHfpAudioChanged &event) {
    Serial.printf("audio=%u rate=%u\n", event.connected,
      static_cast<unsigned>(event.format.sampleRate));
  });
  handsFree.onPcmData([](const EspBluedroidHfpPcmData &pcm) {
    // Copy pcm.data to a bounded speaker queue before returning.
    (void)pcm;
  });
  handsFree.onPcmRequested([](EspBluedroidHfpPcmRequest &request) {
    // Replace silence with microphone PCM from a bounded queue.
    memset(request.data, 0, request.capacity);
    request.written = request.capacity;
  });
  if (!handsFree.start())
    Serial.printf("HFP error: %s\n", bluetooth.lastErrorDetail().c_str());
}

void loop()
{
  bluetooth.update();
  if (Serial.available())
  {
    const String address = Serial.readStringUntil('\n');
    if (!bluetooth.classic().hfpHandsFree().connect(address.c_str()))
      Serial.printf("Connect error: %s\n", bluetooth.lastErrorDetail().c_str());
  }
  delay(1);
}
