// Cross-stack profile values: the shared codec headers produce and accept the
// same bytes when the two ends are different libraries. This is the `peer_device`
// half — the board running the library under test, here as the GATT **server** and
// then as a beacon — while the ESP32-S3 running EspBle is the parent fixture and
// the central.
//
// `EspBleMedicalFloat.h`, `EspBleCgmCrc.h` and `EspBleIBeacon.h` are verbatim
// copies of EspBle's, and `unit/` already checks each copy against its own
// vectors. What no unit test can show is the round trip: this board encodes with
// its copy, the bytes cross the air, and the *other* library decodes with its own
// compiled copy — and back the other way for the write. A copy that silently
// drifted (endianness, the SFLOAT exponent nibble, the reflected CRC polynomial)
// fails here even though both sides' unit tests still pass.
//
// The values are integers in milli-units on both sides, so the comparison is
// exact and never a float formatting difference.
//
// Every step is driven by a serial command, so a failure names the step.

#include <EspBleBluedroid.h>
#include <EspBleCgmCrc.h>
#include <EspBleIBeacon.h>
#include <EspBleMedicalFloat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Interop UUIDs live in the 01xx suite-tag range (tests/TEST_PLAN.md).
static constexpr const char *SERVICE_UUID =
  "01050000-b1dd-4d00-9e5a-627564726f69";
// A FLOAT32 temperature, in the Health Thermometer Measurement shape.
static constexpr const char *TEMPERATURE_UUID =
  "01050001-b1dd-4d00-9e5a-627564726f69";
// A CGM Measurement with the E2E-CRC appended.
static constexpr const char *GLUCOSE_UUID =
  "01050002-b1dd-4d00-9e5a-627564726f69";
// The other direction: the central writes a value it encoded itself.
static constexpr const char *WRITTEN_UUID =
  "01050003-b1dd-4d00-9e5a-627564726f69";

// 36.50 °C as FLOAT32 (mantissa 3650, exponent -2) and 123.4 mg/dL as SFLOAT
// (mantissa 1234, exponent -1). Both are the awkward cases on purpose: a signed
// exponent packed into the top nibble, little-endian on the wire.
static constexpr int32_t TEMPERATURE_MANTISSA = 3650;
static constexpr int8_t TEMPERATURE_EXPONENT = -2;
static constexpr int16_t GLUCOSE_MANTISSA = 1234;
static constexpr int8_t GLUCOSE_EXPONENT = -1;

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
bool ready = false;
EspBleGattCharacteristic temperature;
EspBleGattCharacteristic glucose;
EspBleGattCharacteristic written;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

// Health Thermometer Measurement: flags octet then a FLOAT32, little-endian.
size_t buildTemperature(uint8_t *out)
{
  out[0] = 0x00; // Celsius, no timestamp, no type
  espBleWriteMedicalFloat32LE(out + 1, TEMPERATURE_MANTISSA, TEMPERATURE_EXPONENT);
  return 5;
}

// CGM Measurement: size, flags, an SFLOAT concentration, a time offset, and the
// E2E-CRC over everything before it.
size_t buildGlucose(uint8_t *out)
{
  out[0] = 0x08; // size including the CRC
  out[1] = 0x03; // E2E-CRC supported and present
  espBleWriteMedicalSFloatLE(out + 2, GLUCOSE_MANTISSA, GLUCOSE_EXPONENT);
  out[4] = 0x2a; // time offset, minutes, little-endian
  out[5] = 0x00;
  return espBleCgmAppendCrc(out, 6);
}

void reportRegistration()
{
  Serial.printf("PROFILE_WIRE_STATE ready=%u\n", ready ? 1 : 0);
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();

  auto &server = bluetooth.gattServer();
  EspBleGattCharacteristicConfig readableConfig;
  readableConfig.readable = true;
  readableConfig.notifiable = true;
  EspBleGattCharacteristicConfig writableConfig;
  writableConfig.writable = true;

  const EspBleGattService service = server.addService(SERVICE_UUID);
  temperature = server.addCharacteristic(service, TEMPERATURE_UUID, readableConfig);
  glucose = server.addCharacteristic(service, GLUCOSE_UUID, readableConfig);
  written = server.addCharacteristic(service, WRITTEN_UUID, writableConfig);

  uint8_t temperatureValue[5];
  uint8_t glucoseValue[8];
  const size_t temperatureLength = buildTemperature(temperatureValue);
  const size_t glucoseLength = buildGlucose(glucoseValue);
  if (!service || !temperature || !glucose || !written ||
      !server.setValue(temperature, temperatureValue, temperatureLength) ||
      !server.setValue(glucose, glucoseValue, glucoseLength))
  {
    Serial.printf("CONFIG_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }

  server.onWritten([](const EspBleGattWrite &write) {
    // Decoded with this library's copy of the codec, from bytes the other
    // library's copy produced.
    if (write.value.length() < 3)
    {
      Serial.printf("WRITTEN_SHORT length=%u\n",
        static_cast<unsigned>(write.value.length()));
      return;
    }
    const uint8_t *data = reinterpret_cast<const uint8_t *>(write.value.c_str());
    const double value = espBleReadMedicalSFloatLE(data + 1);
    Serial.printf("WRITTEN flags=%02x milli=%ld length=%u context=%s\n",
      data[0], lround(value * 1000.0),
      static_cast<unsigned>(write.value.length()), contextName());
  });
  server.onSubscriptionChanged([](const EspBleGattSubscription &event) {
    Serial.printf("SUBSCRIPTION notifications=%u context=%s\n",
      event.notifications ? 1 : 0, contextName());
  });
  server.onSent([](const EspBleGattSendResult &result) {
    Serial.printf("SENT success=%u indication=%u context=%s\n",
      result.success ? 1 : 0, result.indication ? 1 : 0, contextName());
  });

  EspBleConfig config;
  config.deviceName = "Bluedroid Profile Wire";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BEGIN_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }
  // The 128-bit service UUID plus flags take 21 of the 31 advertising bytes, so
  // the name is left to the scan response the backend fills in.
  bluetooth.advertising().addServiceUuid(SERVICE_UUID);
  if (!bluetooth.advertising().start())
  {
    Serial.printf("ADVERTISE_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  ready = true;
  Serial.println("INTEROP_PROFILE_WIRE_READY");
}

void loop()
{
  bluetooth.update();
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == '?')
    {
      reportRegistration();
    }
    else if (command == 'n')
    {
      uint8_t value[5];
      const size_t length = buildTemperature(value);
      Serial.printf("NOTIFY_ACCEPTED %u\n",
        bluetooth.gattServer().notify(temperature, value, length) ? 1 : 0);
    }
    else if (command == 'i')
    {
      // Same codec, no connection: the beacon payload is built here and decoded
      // by the other library's copy from the advertisement alone.
      EspBleIBeaconData beacon;
      static const uint8_t uuid[16] = {
        0x01, 0x05, 0x01, 0x00, 0xb1, 0xdd, 0x4d, 0x00,
        0x9e, 0x5a, 0x62, 0x75, 0x64, 0x72, 0x6f, 0x69};
      for (size_t index = 0; index < 16; ++index) beacon.uuid[index] = uuid[index];
      beacon.major = 0x0105;
      beacon.minor = 0x2b1d;
      beacon.measuredPower = -59;
      uint8_t payload[EspBleIBeaconManufacturerDataSize];
      const size_t length = espBleEncodeIBeacon(beacon, payload);

      auto &advertising = bluetooth.advertising();
      advertising.stop();
      advertising.clear();
      advertising.setConnectable(false);
      advertising.setScanResponseEnabled(false);
      advertising.setManufacturerData(payload, length);
      advertising.setInterval(100, 150);
      Serial.printf("BEACON_STARTED %u length=%u error=%s\n",
        advertising.start() && advertising.isAdvertising() ? 1 : 0,
        static_cast<unsigned>(length), bluetooth.lastErrorName());
    }
  }
  delay(1);
}
