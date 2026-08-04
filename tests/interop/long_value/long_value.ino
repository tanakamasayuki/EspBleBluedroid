// The EspBle (NimBLE) half of the cross-stack long-value scenario, running on an
// ESP32-S3 against the released EspBle pinned in sketch.yaml.
//
// This board publishes one Characteristic whose value is longer than any single
// ATT response on the link. `peer/long_value` already pins that Bluedroid returns
// the whole value rather than truncating it at the MTU — the documentation here
// assumed the opposite until that test ran — but both ends of it are Bluedroid,
// so the continuation could be Bluedroid answering Bluedroid. Here the responder
// is NimBLE, which is what makes the claim about the client rather than about the
// pair.
//
// Output is prefixed ESPBLE_ so a log line never leaves it ambiguous which stack
// produced it.

#include <EspBle.h>

// Interop UUIDs live in the 01xx suite-tag range (tests/TEST_PLAN.md).
static constexpr const char *SERVICE_UUID =
  "01020000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *CHARACTERISTIC_UUID =
  "01020001-b1dd-4d00-9e5a-627564726f69";

// Longer than the 247-byte MTU both libraries prefer, so the read has to be
// continued whatever the negotiated value turns out to be. The same ramp
// (byte i = i & 0xff) as `peer/long_value`, per the shared-expectations rule in
// tests/TEST_PLAN.md: a value reassembled out of order fails, where a length
// check alone would pass.
static constexpr size_t VALUE_LENGTH = 300;

EspBle ble;
bool ready = false;

void setup()
{
  Serial.begin(115200);
  delay(500);

  auto &server = ble.gattServer();
  EspBleGattCharacteristicConfig characteristicConfig;
  characteristicConfig.readable = true;

  const EspBleGattService service = server.addService(SERVICE_UUID);
  const EspBleGattCharacteristic characteristic =
    server.addCharacteristic(service, CHARACTERISTIC_UUID, characteristicConfig);

  uint8_t value[VALUE_LENGTH];
  for (size_t index = 0; index < VALUE_LENGTH; ++index)
  {
    value[index] = static_cast<uint8_t>(index & 0xff);
  }
  if (!service.valid() || !characteristic.valid() ||
      !server.setValue(characteristic, value, sizeof(value)))
  {
    Serial.printf("ESPBLE_CONFIG_FAILED %s %s\n", ble.lastErrorName(),
      ble.lastErrorDetail().c_str());
    return;
  }

  EspBleConfig config;
  config.deviceName = "EspBle Long Value";
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
  Serial.printf("ESPBLE_LONG_VALUE_READY length=%u\n",
    static_cast<unsigned>(VALUE_LENGTH));
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
      Serial.printf("ESPBLE_LONG_VALUE_STATE ready=%u length=%u\n",
        ready ? 1 : 0, static_cast<unsigned>(VALUE_LENGTH));
    }
  }
  delay(1);
}
