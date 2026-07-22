#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID =
  "8d47a650-8d3a-4d65-a76f-6f626c756564";
static constexpr const char *CHARACTERISTIC_UUID =
  "8d47a651-8d3a-4d65-a76f-6f626c756564";

EspBleBluedroid bluetooth;
bool connectionRequested = false;

void setup()
{
  Serial.begin(115200);
  if (!bluetooth.begin()) return;

  bluetooth.onConnected([](const EspBleConnection &connection) {
    bluetooth.readCharacteristic(
      connection.id, SERVICE_UUID, CHARACTERISTIC_UUID);
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    if (!result.success) return;
    bluetooth.writeCharacteristic(
      result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID,
      String("hello from Central"), true);
  });
  bluetooth.onCharacteristicWritten([](const EspBleGattResult &result) {
    if (!result.success) return;
    bluetooth.subscribe(
      result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID, true);
  });
  bluetooth.onSubscribed([](const EspBleGattResult &result) {
    Serial.println(result.success ? "Subscribed" : "Subscribe failed");
  });
  bluetooth.onNotification([](const EspBleGattNotification &notification) {
    Serial.printf("Notification length: %u\n",
      static_cast<unsigned>(notification.value.length()));
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
