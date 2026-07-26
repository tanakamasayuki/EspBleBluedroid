#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;
EspBluedroidSppSerial sppSerial(bluetooth);

void setup()
{
  Serial.begin(115200);
  delay(1500);

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Serial Client";
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
      Serial.println("Enter the peer Classic address to reconnect");
    });
  bluetooth.classic().spp().onConnectionFailed(
    [](const EspBluedroidSppConnectionFailure &failure) {
      Serial.printf("connect failed: %s (%s)\n",
        failure.peerAddress.c_str(), failure.detail.c_str());
      Serial.println("Enter the peer Classic address to retry");
    });

  Serial.println("Enter a Classic address such as 01:23:45:67:89:ab");
}

void loop()
{
  bluetooth.update();

  while (sppSerial.available() > 0)
  {
    Serial.write(sppSerial.read());
  }

  if (!sppSerial.connected() && Serial.available() > 0)
  {
    String address = Serial.readStringUntil('\n');
    address.trim();
    if (address.length() > 0 &&
        !bluetooth.classic().spp().connect(address.c_str()))
    {
      Serial.printf("connect request rejected: %s (%s)\n",
        bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    }
  }
  else
  {
    while (Serial.available() > 0)
    {
      sppSerial.write(Serial.read());
    }
  }
  delay(1);
}
