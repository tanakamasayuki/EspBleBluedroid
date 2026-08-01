#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *SERVICE_UUID =
  "6b976b10-5e89-4e3f-8a94-676174747372";
static constexpr const char *CHARACTERISTIC_UUID =
  "6b976b11-5e89-4e3f-8a94-676174747372";
static constexpr const char *DESCRIPTOR_UUID =
  "6b976b12-5e89-4e3f-8a94-676174747372";

EspBleBluedroid bluetooth;
EspBleGattCharacteristic characteristic;
TaskHandle_t loopTask;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();
  auto &server = bluetooth.gattServer();
  EspBleGattCharacteristicConfig characteristicConfig;
  characteristicConfig.readable = true;
  characteristicConfig.writable = true;
  characteristicConfig.notifiable = true;
  EspBleGattDescriptorConfig descriptorConfig;
  descriptorConfig.writable = true;
  const EspBleGattService service = server.addService(SERVICE_UUID);
  characteristic = server.addCharacteristic(
    service, CHARACTERISTIC_UUID, characteristicConfig);
  const EspBleGattDescriptor descriptor = server.addDescriptor(
    characteristic, DESCRIPTOR_UUID, descriptorConfig);
  const uint8_t initial[] = {0x49, 0x00, 0xff};
  const uint8_t descriptorInitial[] = {0x44, 0x00};
  if (!service || !characteristic || !descriptor ||
      !server.setValue(characteristic, initial, sizeof(initial)) ||
      !server.setDescriptorValue(
        descriptor, descriptorInitial, sizeof(descriptorInitial)))
  {
    Serial.printf("SERVER_CONFIG_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  server.onRead([](const EspBleGattReadRequest &request) {
    const uint8_t value[] = {0x52, 0x00, 0xfe};
    bluetooth.gattServer().setValue(request.characteristic, value, sizeof(value));
  });
  server.onWritten([](const EspBleGattWrite &write) {
    Serial.printf("SERVER_WRITE id=%u length=%u hex=",
      static_cast<unsigned>(write.connectionId),
      static_cast<unsigned>(write.value.length()));
    for (size_t i = 0; i < write.value.length(); ++i)
      Serial.printf("%02x", static_cast<uint8_t>(write.value[i]));
    Serial.printf(" context=%s\n", contextName());
  });
  server.onDescriptorWritten([](const EspBleGattDescriptorWrite &write) {
    Serial.printf("SERVER_DESCRIPTOR_WRITE length=%u context=%s\n",
      static_cast<unsigned>(write.value.length()), contextName());
  });
  server.onSubscriptionChanged([](const EspBleGattSubscription &event) {
    Serial.printf("SERVER_SUBSCRIPTION notifications=%u context=%s\n",
      event.notifications ? 1 : 0, contextName());
  });
  server.onSent([](const EspBleGattSendResult &result) {
    Serial.printf("SERVER_SENT success=%u indication=%u context=%s\n",
      result.success ? 1 : 0, result.indication ? 1 : 0, contextName());
  });
  EspBleConfig config;
  config.deviceName = "Bluedroid GATT Server Test";
  if (!bluetooth.begin(config))
  {
    Serial.printf("SERVER_BEGIN_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }
  bluetooth.advertising().setName(config.deviceName);
  bluetooth.advertising().addServiceUuid(SERVICE_UUID);
  bluetooth.advertising().start();
  Serial.println("GATT_SERVER_READY");
}

void loop()
{
  bluetooth.update();
  if (Serial.available() && Serial.read() == 'n')
  {
    const uint8_t value[] = {0x4e, 0x00, 0xfd};
    Serial.printf("SERVER_NOTIFY_ACCEPTED %u\n",
      bluetooth.gattServer().notify(characteristic, value, sizeof(value)) ? 1 : 0);
  }
  delay(1);
}
