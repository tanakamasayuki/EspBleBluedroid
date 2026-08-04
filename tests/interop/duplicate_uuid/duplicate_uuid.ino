// The EspBle (NimBLE) half of the cross-stack duplicate-UUID scenario, running on
// an ESP32-S3 against the released EspBle pinned in sketch.yaml.
//
// This board publishes what the spec allows and this library's server API cannot:
// one Service carrying two Characteristics that share a UUID (HID Reports are the
// everyday case). Only a handle can tell them apart, so it is the peer this
// library's client half needs in order to be exercised honestly. `peer/
// duplicate_uuid` builds the same peer out of the bundled Arduino Bluedroid
// wrapper; here the responder is NimBLE, so the routing is verified against a
// second implementation rather than against the one this library sits on.
//
// Both directions of routing are reported: which of the two attributes a write
// landed on, and notifications are sent from the second one, whose value the
// client must receive tagged with the second handle.
//
// Output is prefixed ESPBLE_ so a log line never leaves it ambiguous which stack
// produced it.

#include <EspBle.h>

// Interop UUIDs live in the 01xx suite-tag range (tests/TEST_PLAN.md).
static constexpr const char *SERVICE_UUID =
  "01030000-b1dd-4d00-9e5a-627564726f69";
// One UUID, two Characteristics inside the Service above.
static constexpr const char *CHARACTERISTIC_UUID =
  "01030001-b1dd-4d00-9e5a-627564726f69";

// Distinct values, so a read that reached the wrong attribute is visible instead
// of merely unproven.
static constexpr uint8_t FIRST_VALUE[] = {0x41, 0x00, 0xf1};
static constexpr uint8_t SECOND_VALUE[] = {0x42, 0x00, 0xf2};
static constexpr uint8_t SECOND_NOTIFY_VALUE[] = {0x43, 0x00, 0xf3};

EspBle ble;
bool ready = false;
EspBleGattCharacteristic first;
EspBleGattCharacteristic second;

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

  auto &server = ble.gattServer();
  EspBleGattCharacteristicConfig characteristicConfig;
  characteristicConfig.readable = true;
  characteristicConfig.writable = true;
  characteristicConfig.notifiable = true;

  const EspBleGattService service = server.addService(SERVICE_UUID);
  first = server.addCharacteristic(service, CHARACTERISTIC_UUID, characteristicConfig);
  second = server.addCharacteristic(service, CHARACTERISTIC_UUID, characteristicConfig);
  if (!service.valid() || !first.valid() || !second.valid() || first == second ||
      !server.setValue(first, FIRST_VALUE, sizeof(FIRST_VALUE)) ||
      !server.setValue(second, SECOND_VALUE, sizeof(SECOND_VALUE)))
  {
    Serial.printf("ESPBLE_CONFIG_FAILED %s %s\n", ble.lastErrorName(),
      ble.lastErrorDetail().c_str());
    return;
  }

  server.onWritten([](const EspBleGattWrite &write) {
    // Which attribute the write landed on. The UUIDs cannot tell them apart, so
    // the handle kept from addCharacteristic() is the only answer.
    Serial.printf("ESPBLE_WRITE which=%u hex=",
      write.characteristic == first ? 1 : (write.characteristic == second ? 2 : 0));
    printHex(write.value);
    Serial.println();
  });
  server.onSubscriptionChanged([](const EspBleGattSubscription &subscription) {
    Serial.printf("ESPBLE_SUBSCRIPTION which=%u notifications=%u indications=%u\n",
      subscription.characteristic == first
        ? 1 : (subscription.characteristic == second ? 2 : 0),
      subscription.notifications ? 1 : 0, subscription.indications ? 1 : 0);
  });

  EspBleConfig config;
  config.deviceName = "EspBle Duplicate UUID";
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
  Serial.println("ESPBLE_DUPLICATE_UUID_READY");
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
      // would depend on when the monitor started reading. `distinct` is the
      // premise of the whole scenario: the peer really did register two
      // attributes rather than reusing the first.
      Serial.printf("ESPBLE_DUPLICATE_STATE ready=%u distinct=%u\n",
        ready ? 1 : 0,
        first.valid() && second.valid() && first != second ? 1 : 0);
    }
    else if (command == 'n')
    {
      // From the second attribute only: the client has to receive it tagged with
      // the second handle, not with the first one sharing the UUID.
      Serial.printf("ESPBLE_NOTIFY_ACCEPTED %u\n",
        ble.gattServer().notify(
          second, SECOND_NOTIFY_VALUE, sizeof(SECOND_NOTIFY_VALUE)) ? 1 : 0);
    }
  }
  delay(1);
}
