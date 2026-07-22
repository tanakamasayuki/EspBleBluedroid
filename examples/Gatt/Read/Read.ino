#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID = "180f";
static constexpr const char *CHARACTERISTIC_UUID = "2a19";

EspBleBluedroid bluetooth;
bool connectionRequested = false;

void setup()
{
  Serial.begin(115200);
  if (!bluetooth.begin()) return;

  bluetooth.onConnected([](const EspBleConnection &connection) {
    if (!bluetooth.readCharacteristic(
          connection.id, SERVICE_UUID, CHARACTERISTIC_UUID))
    {
      Serial.printf("Read request failed: %s\n",
        bluetooth.lastErrorDetail().c_str());
    }
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    if (result.success)
    {
      Serial.printf("Battery value length: %u\n",
        static_cast<unsigned>(result.value.length()));
      const uint8_t value[] = {0x00, 0x64};
      bluetooth.writeCharacteristic(
        result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID,
        value, sizeof(value), true);
    }
    else
    {
      Serial.printf("Read failed: %s\n", result.detail.c_str());
    }
  });
  bluetooth.onCharacteristicWritten([](const EspBleGattResult &result) {
    Serial.println(result.success ? "Write complete" : "Write failed");
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested || !result.advertisesService(SERVICE_UUID)) return;
    bluetooth.scanner().stop();
    connectionRequested = bluetooth.connect(result);
  });
  bluetooth.scanner().start();
}

void loop()
{
  bluetooth.update();
  delay(1);
}
