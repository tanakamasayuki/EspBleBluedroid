#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID =
  "8d47a650-8d3a-4d65-a76f-6f626c756564";
static constexpr const char *CHARACTERISTIC_UUID =
  "8d47a651-8d3a-4d65-a76f-6f626c756564";
static constexpr const char *DESCRIPTOR_UUID =
  "8d47a652-8d3a-4d65-a76f-6f626c756564";

EspBleBluedroid bluetooth;
bool connectionRequested = false;
uint16_t targetCharacteristicHandle = 0;

void setup()
{
  Serial.begin(115200);
  if (!bluetooth.begin()) return;

  bluetooth.onConnected([](const EspBleConnection &connection) {
    bluetooth.discoverServices(connection.id);
  });
  bluetooth.onServicesDiscovered([](const EspBleGattResult &result) {
    if (!result.success) return;
    Serial.printf("Discovered services: %u\n",
      static_cast<unsigned>(
        bluetooth.discoveredServiceCount(result.connectionId)));
    const size_t count = bluetooth.discoveredCharacteristicCount(
      result.connectionId, SERVICE_UUID);
    for (size_t index = 0; index < count; ++index)
    {
      EspBleGattCharacteristicInfo info;
      if (bluetooth.discoveredCharacteristic(
            result.connectionId, index, info, SERVICE_UUID) &&
          info.characteristicUuid.equalsIgnoreCase(CHARACTERISTIC_UUID))
      {
        targetCharacteristicHandle = info.handle;
        break;
      }
    }
    if (targetCharacteristicHandle == 0) return;
    bluetooth.readDescriptor(
      result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID, DESCRIPTOR_UUID);
  });
  bluetooth.onDescriptorRead([](const EspBleGattResult &result) {
    if (!result.success) return;
    bluetooth.writeDescriptor(
      result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID, DESCRIPTOR_UUID,
      String("Central descriptor"), true);
  });
  bluetooth.onDescriptorWritten([](const EspBleGattResult &result) {
    if (!result.success) return;
    bluetooth.readCharacteristic(
      result.connectionId, targetCharacteristicHandle);
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    if (!result.success) return;
    bluetooth.writeCharacteristic(
      result.connectionId, result.handle, String("hello from Central"), true);
  });
  bluetooth.onCharacteristicWritten([](const EspBleGattResult &result) {
    if (!result.success) return;
    bluetooth.subscribe(
      result.connectionId, result.handle, true);
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
