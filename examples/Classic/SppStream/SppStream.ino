#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;
EspBluedroidSppStream sppSerial;

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
      if (sppSerial.attach(bluetooth.classic().spp(), session.id))
      {
        sppSerial.println("EspBleBluedroid SPP stream ready");
      }
    });
  bluetooth.classic().spp().onDisconnected(
    [](const EspBluedroidSppSession &) {
      sppSerial.detach();
      Serial.println("SPP stream disconnected");
    });

  EspBluedroidSppServerConfig config;
  config.serviceName = "EspBleBluedroid Stream";
  if (!bluetooth.classic().spp().startServer(config))
  {
    Serial.printf("startServer failed: %s\n", bluetooth.lastErrorName());
  }
}

void loop()
{
  bluetooth.update();

  while (sppSerial.available())
  {
    const int value = sppSerial.read();
    if (value == '\r' || value == '\n')
    {
      sppSerial.println();
    }
    else
    {
      sppSerial.write(static_cast<uint8_t>(value));
    }
  }
  delay(1);
}
