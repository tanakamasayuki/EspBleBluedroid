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

  bluetooth.onConnected([](const EspBleConnection &connection) {
    const bool accepted = bluetooth.readCharacteristic(
      connection.id, SERVICE_UUID, CHARACTERISTIC_UUID, 5000);
    Serial.printf("READ_REQUESTED %u\n", accepted ? 1 : 0);
    updatesEnabled = false;
    Serial.println("GATT_UPDATE_PAUSED");
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
    bluetooth.disconnect(result.connectionId);
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
