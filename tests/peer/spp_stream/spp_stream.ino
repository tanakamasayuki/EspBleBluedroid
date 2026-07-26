#include <EspBleBluedroid.h>
#include <esp_bt_device.h>

EspBleBluedroid bluetooth;
EspBluedroidSppStream sppSerial;
bool initialized = false;
bool replied = false;

String localAddress()
{
  const uint8_t *address = esp_bt_dev_get_address();
  char value[18];
  snprintf(value, sizeof(value), "%02x:%02x:%02x:%02x:%02x:%02x",
    address[0], address[1], address[2], address[3], address[4], address[5]);
  return String(value);
}

void initializeBluetooth()
{
  Serial.printf("SPP_STREAM_DEFAULT connected=%u id=%u\n",
    sppSerial.connected() ? 1 : 0,
    static_cast<unsigned>(sppSerial.sessionId()));
  if (!bluetooth.begin())
  {
    Serial.printf("SPP_STREAM_INIT_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  bluetooth.classic().spp().onServerStarted([]() {
    Serial.printf("SPP_STREAM_SERVER_READY address=%s\n",
      localAddress().c_str());
  });
  bluetooth.classic().spp().onConnected(
    [](const EspBluedroidSppSession &session) {
      const bool invalid =
        sppSerial.attach(bluetooth.classic().spp(), session.id + 100);
      const bool attached =
        sppSerial.attach(bluetooth.classic().spp(), session.id);
      EspBluedroidSppStream constructed(
        bluetooth.classic().spp(), session.id);
      Stream *stream = &sppSerial;
      Serial.printf(
        "SPP_STREAM_ATTACHED invalid=%u attached=%u connected=%u "
        "id=%u stream=%u constructed=%u writable=%d\n",
        invalid ? 1 : 0, attached ? 1 : 0,
        sppSerial ? 1 : 0, static_cast<unsigned>(sppSerial.sessionId()),
        stream == &sppSerial ? 1 : 0, constructed.connected() ? 1 : 0,
        sppSerial.availableForWrite());
    });
  bluetooth.classic().spp().onDisconnected(
    [](const EspBluedroidSppSession &) {
      const size_t rejected = sppSerial.write(static_cast<uint8_t>('x'));
      Serial.printf(
        "SPP_STREAM_DISCONNECTED connected=%u available=%d "
        "peek=%d read=%d write=%u\n",
        sppSerial.connected() ? 1 : 0, sppSerial.available(),
        sppSerial.peek(), sppSerial.read(),
        static_cast<unsigned>(rejected));
      sppSerial.detach();
      Serial.printf("SPP_STREAM_DETACHED id=%u\n",
        static_cast<unsigned>(sppSerial.sessionId()));
    });

  EspBluedroidSppServerConfig config;
  config.serviceName = "EspBleBluedroid Stream";
  bluetooth.classic().spp().startServer(config);
}

void setup()
{
  Serial.begin(115200);
  delay(500);
}

void loop()
{
  if (Serial.available() && Serial.read() == 'i' && !initialized)
  {
    initialized = true;
    initializeBluetooth();
  }
  bluetooth.update();

  if (!replied && sppSerial.available() >= 3)
  {
    replied = true;
    const int first = sppSerial.peek();
    const int single = sppSerial.read();
    uint8_t remaining[2] = {};
    const size_t read = sppSerial.readBytes(remaining, sizeof(remaining));
    const size_t printed =
      sppSerial.print("value=") +
      sppSerial.print(42) +
      sppSerial.println();
    static uint8_t binary[1000];
    for (size_t index = 0; index < sizeof(binary); ++index)
    {
      binary[index] = static_cast<uint8_t>(index % 251);
    }
    const size_t binaryWritten = sppSerial.write(binary, sizeof(binary));
    sppSerial.flush();
    Serial.printf(
      "SPP_STREAM_IO first=%d single=%d remaining=%02x%02x "
      "available=%d printed=%u binary=%u\n",
      first, single, remaining[0], remaining[1], sppSerial.available(),
      static_cast<unsigned>(printed),
      static_cast<unsigned>(binaryWritten));
  }
  delay(1);
}
