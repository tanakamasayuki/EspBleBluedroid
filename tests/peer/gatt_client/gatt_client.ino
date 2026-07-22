#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *SERVICE_UUID =
  "8d47a650-8d3a-4d65-a76f-6f626c756564";
static constexpr const char *CHARACTERISTIC_UUID =
  "8d47a651-8d3a-4d65-a76f-6f626c756564";

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
bool connectionRequested = false;
bool updatesEnabled = true;
uint8_t writePhase = 0;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();

  if (!bluetooth.begin())
  {
    Serial.printf("INIT_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  const bool invalidAccepted = bluetooth.readCharacteristic(
    999, SERVICE_UUID, CHARACTERISTIC_UUID, 1000);
  Serial.printf("INVALID_READ_REJECTED %u error=%s\n",
    invalidAccepted ? 0 : 1, bluetooth.lastErrorName());
  const bool invalidWriteAccepted = bluetooth.writeCharacteristic(
    999, SERVICE_UUID, CHARACTERISTIC_UUID, nullptr, 1, true, 1000);
  Serial.printf("INVALID_WRITE_REJECTED %u error=%s\n",
    invalidWriteAccepted ? 0 : 1, bluetooth.lastErrorName());

  bluetooth.onConnected([](const EspBleConnection &connection) {
    const bool accepted = bluetooth.discoverServices(connection.id, 5000);
    Serial.printf("DISCOVERY_REQUESTED %u\n", accepted ? 1 : 0);
    const uint8_t concurrentPayload = 0x01;
    const bool concurrentAccepted = bluetooth.writeCharacteristic(
      connection.id, SERVICE_UUID, CHARACTERISTIC_UUID,
      &concurrentPayload, 1, true, 5000);
    Serial.printf("CONCURRENT_GATT_REJECTED %u error=%s\n",
      concurrentAccepted ? 0 : 1, bluetooth.lastErrorName());
    updatesEnabled = false;
    Serial.println("GATT_UPDATE_PAUSED");
  });
  bluetooth.onServicesDiscovered([](const EspBleGattResult &result) {
    bool found = false;
    bool cccdFound = false;
    bool propertiesValid = false;
    for (size_t index = 0;
         index < bluetooth.discoveredCharacteristicCount(result.connectionId);
         ++index)
    {
      EspBleGattCharacteristicInfo info;
      if (bluetooth.discoveredCharacteristic(
            result.connectionId, index, info) &&
          info.serviceUuid.equalsIgnoreCase(SERVICE_UUID) &&
          info.characteristicUuid.equalsIgnoreCase(CHARACTERISTIC_UUID))
      {
        found = info.handle != 0;
        propertiesValid = info.readable && info.writable &&
          info.writableWithoutResponse && info.notifiable;
      }
    }
    const size_t descriptorCount = bluetooth.discoveredDescriptorCount(
      result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID);
    for (size_t index = 0; index < descriptorCount; ++index)
    {
      EspBleGattDescriptorInfo info;
      if (bluetooth.discoveredDescriptor(
            result.connectionId, index, info,
            SERVICE_UUID, CHARACTERISTIC_UUID))
      {
        cccdFound = info.handle != 0;
      }
    }
    Serial.printf(
      "DISCOVERY success=%u services=%u found=%u cccd=%u properties=%u context=%s\n",
      result.success ? 1 : 0,
      static_cast<unsigned>(bluetooth.discoveredServiceCount(result.connectionId)),
      found ? 1 : 0, cccdFound ? 1 : 0, propertiesValid ? 1 : 0,
      contextName());
    const bool accepted = bluetooth.readCharacteristic(
      result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID, 5000);
    Serial.printf("READ_REQUESTED %u\n", accepted ? 1 : 0);
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    const bool valid = result.success && result.value.length() == 4 &&
      static_cast<uint8_t>(result.value[0]) == 0x00 &&
      static_cast<uint8_t>(result.value[1]) == 0x41 &&
      static_cast<uint8_t>(result.value[2]) == 0xff &&
      static_cast<uint8_t>(result.value[3]) == 0x42;
    Serial.printf(
      "READ_RESULT success=%u length=%u hex=%02x%02x%02x%02x context=%s\n",
      valid ? 1 : 0, static_cast<unsigned>(result.value.length()),
      result.value.length() > 0 ? static_cast<uint8_t>(result.value[0]) : 0,
      result.value.length() > 1 ? static_cast<uint8_t>(result.value[1]) : 0,
      result.value.length() > 2 ? static_cast<uint8_t>(result.value[2]) : 0,
      result.value.length() > 3 ? static_cast<uint8_t>(result.value[3]) : 0,
      contextName());
    const uint8_t payload[] = {0x00, 0x57, 0xff};
    const bool accepted = bluetooth.writeCharacteristic(
      result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID,
      payload, sizeof(payload), true, 5000);
    Serial.printf("WRITE_RESPONSE_REQUESTED %u\n", accepted ? 1 : 0);
  });
  bluetooth.onCharacteristicWritten([](const EspBleGattResult &result) {
    Serial.printf(
      "WRITE_RESULT phase=%u success=%u response=%u length=%u context=%s\n",
      static_cast<unsigned>(writePhase), result.success ? 1 : 0,
      result.response ? 1 : 0, static_cast<unsigned>(result.value.length()),
      contextName());
    if (writePhase++ == 0)
    {
      const uint8_t payload[] = {0x4e, 0x00, 0x52};
      const bool accepted = bluetooth.writeCharacteristic(
        result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID,
        payload, sizeof(payload), false, 5000);
      Serial.printf("WRITE_NO_RESPONSE_REQUESTED %u\n", accepted ? 1 : 0);
    }
    else
    {
      const bool accepted = bluetooth.subscribe(
        result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID, true, 5000);
      Serial.printf("SUBSCRIBE_REQUESTED %u\n", accepted ? 1 : 0);
    }
  });
  bluetooth.onSubscribed([](const EspBleGattResult &result) {
    Serial.printf("SUBSCRIBED success=%u notifications=%u context=%s\n",
      result.success ? 1 : 0,
      result.subscribedToNotifications ? 1 : 0, contextName());
  });
  bluetooth.onNotification([](const EspBleGattNotification &notification) {
    const bool valid = notification.value.length() == 4 &&
      static_cast<uint8_t>(notification.value[0]) == 0x4e &&
      static_cast<uint8_t>(notification.value[1]) == 0x00 &&
      static_cast<uint8_t>(notification.value[2]) == 0xff &&
      static_cast<uint8_t>(notification.value[3]) == 0x59;
    Serial.printf("NOTIFICATION valid=%u indication=%u context=%s\n",
      valid ? 1 : 0, notification.indication ? 1 : 0, contextName());
    const bool accepted = bluetooth.unsubscribe(
      notification.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID, 5000);
    Serial.printf("UNSUBSCRIBE_REQUESTED %u\n", accepted ? 1 : 0);
  });
  bluetooth.onUnsubscribed([](const EspBleGattResult &result) {
    Serial.printf("UNSUBSCRIBED success=%u context=%s\n",
      result.success ? 1 : 0, contextName());
    bluetooth.disconnect(result.connectionId);
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf("GATT_DISCONNECTED snapshot_services=%u context=%s\n",
      static_cast<unsigned>(
        bluetooth.discoveredServiceCount(connection.id)), contextName());
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested || !result.advertisesService(SERVICE_UUID)) return;
    bluetooth.scanner().stop();
    connectionRequested = bluetooth.connect(result);
    Serial.printf("CONNECT_REQUESTED %u\n", connectionRequested ? 1 : 0);
  });

  Serial.println("GATT_CENTRAL_READY");
}

void loop()
{
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == 's')
    {
      Serial.printf("SCAN_STARTED %u\n",
        bluetooth.scanner().start() ? 1 : 0);
    }
    else if (command == 'u')
    {
      updatesEnabled = true;
      Serial.println("GATT_UPDATE_START");
    }
  }
  if (updatesEnabled) bluetooth.update();
  delay(1);
}
