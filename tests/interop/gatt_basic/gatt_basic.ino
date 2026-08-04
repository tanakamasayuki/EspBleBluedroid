// The EspBle (NimBLE) half of the cross-stack GATT scenario, running on an
// ESP32-S3 against the released EspBle pinned in sketch.yaml.
//
// Nothing here is specific to interop: it is an ordinary EspBle GATT Server, so
// anything the Bluedroid central on the other board fails to do against it is a
// difference between the two stacks rather than a quirk of the test. Output is
// prefixed ESPBLE_ so a log line never leaves it ambiguous which stack produced
// it.

#include <EspBle.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Interop UUIDs live in the 01xx suite-tag range (tests/TEST_PLAN.md).
static constexpr const char *SERVICE_UUID =
  "01000000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *CHARACTERISTIC_UUID =
  "01000001-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *DESCRIPTOR_UUID =
  "01000002-b1dd-4d00-9e5a-627564726f69";

// The value the central reads, and the bytes it writes back.
static constexpr uint8_t READ_VALUE[] = {0x1a, 0x00, 0xfe, 0x2b};
static constexpr uint8_t DESCRIPTOR_VALUE[] = {0x1d, 0x00, 0xfd};
static constexpr uint8_t NOTIFY_VALUE[] = {0x1c, 0x00, 0xfc, 0x3c};
static constexpr uint8_t INDICATE_VALUE[] = {0x1e, 0x00, 0xfb, 0x4d};

EspBle ble;
bool ready = false;
EspBleGattService service;
EspBleGattCharacteristic characteristic;
EspBleGattDescriptor descriptor;
TaskHandle_t loopTask = nullptr;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void printHex(const String &value)
{
  for (size_t index = 0; index < value.length(); ++index)
  {
    Serial.printf("%02x", static_cast<uint8_t>(value[index]));
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();

  auto &server = ble.gattServer();
  EspBleGattCharacteristicConfig characteristicConfig;
  characteristicConfig.readable = true;
  characteristicConfig.writable = true;
  characteristicConfig.writableWithoutResponse = true;
  characteristicConfig.notifiable = true;
  characteristicConfig.indicatable = true;
  EspBleGattDescriptorConfig descriptorConfig;
  descriptorConfig.readable = true;
  descriptorConfig.writable = true;

  service = server.addService(SERVICE_UUID);
  characteristic =
    server.addCharacteristic(service, CHARACTERISTIC_UUID, characteristicConfig);
  descriptor =
    server.addDescriptor(characteristic, DESCRIPTOR_UUID, descriptorConfig);
  if (!service.valid() || !characteristic.valid() || !descriptor.valid() ||
      !server.setValue(characteristic, READ_VALUE, sizeof(READ_VALUE)) ||
      !server.setDescriptorValue(
        descriptor, DESCRIPTOR_VALUE, sizeof(DESCRIPTOR_VALUE)))
  {
    Serial.printf("ESPBLE_CONFIG_FAILED %s %s\n", ble.lastErrorName(),
      ble.lastErrorDetail().c_str());
    return;
  }

  server.onWritten([](const EspBleGattWrite &write) {
    Serial.printf("ESPBLE_WRITE length=%u hex=",
      static_cast<unsigned>(write.value.length()));
    printHex(write.value);
    Serial.printf(" context=%s\n", contextName());
  });
  server.onSubscriptionChanged([](const EspBleGattSubscription &subscription) {
    Serial.printf("ESPBLE_SUBSCRIPTION notifications=%u indications=%u\n",
      subscription.notifications ? 1 : 0, subscription.indications ? 1 : 0);
  });
  server.onSent([](const EspBleGattSendResult &result) {
    Serial.printf("ESPBLE_SENT success=%u indication=%u\n",
      result.success ? 1 : 0, result.indication ? 1 : 0);
  });
  ble.onConnected([](const EspBleConnection &connection) {
    Serial.printf("ESPBLE_CONNECTED id=%u mtu=%u\n",
      static_cast<unsigned>(connection.id),
      static_cast<unsigned>(connection.mtu));
  });
  ble.onMtuChanged([](const EspBleMtuChanged &event) {
    Serial.printf("ESPBLE_MTU mtu=%u\n",
      static_cast<unsigned>(event.connection.mtu));
  });
  ble.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf("ESPBLE_DISCONNECTED id=%u\n",
      static_cast<unsigned>(connection.id));
    ble.advertising().start();
  });

  EspBleConfig config;
  config.deviceName = "EspBle Interop Peer";
  if (!ble.begin(config))
  {
    Serial.printf("ESPBLE_BEGIN_FAILED %s %s\n", ble.lastErrorName(),
      ble.lastErrorDetail().c_str());
    return;
  }
  ble.advertising().setName(config.deviceName);
  ble.advertising().addServiceUuid(SERVICE_UUID);
  ble.advertising().start();
  ready = true;
  Serial.println("ESPBLE_PERIPHERAL_READY");
}

void loop()
{
  ble.update();
  if (Serial.available())
  {
    const int command = Serial.read();
    if (command == '?')
    {
      // Answer on request. The board finishes booting while the other one is
      // still being flashed, so a test that waited for the startup line alone
      // would depend on when the monitor started reading.
      Serial.printf("ESPBLE_PERIPHERAL_READY advertising=%u\n", ready ? 1 : 0);
    }
    else if (command == 'n')
    {
      Serial.printf("ESPBLE_NOTIFY_ACCEPTED %u\n",
        ble.gattServer().notify(characteristic, NOTIFY_VALUE, sizeof(NOTIFY_VALUE))
          ? 1 : 0);
    }
    else if (command == 'i')
    {
      Serial.printf("ESPBLE_INDICATE_ACCEPTED %u\n",
        ble.gattServer().indicate(
          characteristic, INDICATE_VALUE, sizeof(INDICATE_VALUE)) ? 1 : 0);
    }
  }
  delay(1);
}
