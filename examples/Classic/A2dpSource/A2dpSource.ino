#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Audio Source";
  if (!bluetooth.begin(config))
  {
    Serial.printf("Bluetooth error: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  auto &source = bluetooth.classic().a2dpSource();
  source.onPcmRequested([](EspBluedroidA2dpPcmRequest &request) {
    if (request.flush) return;
    // Replace this silence with PCM read from a bounded audio queue.
    memset(request.data, 0, request.capacity);
    request.written = request.capacity;
  });
  source.onConnected([](const EspBluedroidA2dpSession &session) {
    Serial.printf("Connected: %s, session=%u\n",
      session.peerAddress.c_str(), static_cast<unsigned>(session.id));
    bluetooth.classic().a2dpSource().startStream();
  });
  source.onDisconnected([](const EspBluedroidA2dpSession &session) {
    Serial.printf("Disconnected: session=%u\n",
      static_cast<unsigned>(session.id));
  });

  if (!source.start())
    Serial.printf("A2DP error: %s\n", bluetooth.lastErrorDetail().c_str());
}

void loop()
{
  bluetooth.update();

  if (Serial.available())
  {
    const String address = Serial.readStringUntil('\n');
    if (!bluetooth.classic().a2dpSource().connect(address.c_str()))
      Serial.printf("Connect error: %s\n", bluetooth.lastErrorDetail().c_str());
  }
  delay(1);
}
