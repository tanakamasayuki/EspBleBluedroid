// en: Broadcast a changing sensor value as Service Data without a connection.
// ja: 接続せず、変化するsensor値をService Dataとして放送する例。
#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID = "181a";

EspBleBluedroid bluetooth;
uint32_t lastUpdateMs = 0;
int16_t temperatureCentiCelsius = 2350;

static void publishTemperature()
{
  // en: Signed 0.01-degree units in little-endian order. Embedded zero bytes
  //     are preserved because Service Data is binary.
  // ja: 0.01度単位のsigned 16bit、little-endian。binaryなので途中の0も保持する。
  const uint8_t payload[] = {
    static_cast<uint8_t>(temperatureCentiCelsius & 0xff),
    static_cast<uint8_t>((temperatureCentiCelsius >> 8) & 0xff),
  };

  auto &advertising = bluetooth.advertising();
  advertising.stop();
  if (!advertising.addServiceData(
        SERVICE_UUID, payload, sizeof(payload)) ||
      !advertising.start())
  {
    Serial.printf("Publish failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }
  Serial.printf("Broadcasting %d.%02d degC\n",
    temperatureCentiCelsius / 100,
    abs(temperatureCentiCelsius) % 100);
}

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "Bluedroid Service Data";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE init failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  auto &advertising = bluetooth.advertising();
  advertising.setConnectable(false);
  advertising.setScanResponseEnabled(false);
  advertising.addServiceUuid(SERVICE_UUID);
  advertising.setInterval(500, 600);
  publishTemperature();
}

void loop()
{
  if (millis() - lastUpdateMs >= 5000)
  {
    lastUpdateMs = millis();
    temperatureCentiCelsius += 25;
    if (temperatureCentiCelsius > 2600)
    {
      temperatureCentiCelsius = 2350;
    }
    publishTemperature();
  }
  bluetooth.update();
  delay(1);
}
