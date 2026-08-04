// The EspBle (NimBLE) half of the cross-stack profile-value scenario, running on
// an ESP32-S3 against the released EspBle pinned in sketch.yaml.
//
// This board is the central and the decoder: it reads the values the other board
// built with its copies of `EspBleMedicalFloat.h` / `EspBleCgmCrc.h` and decodes
// them with the *released* EspBle copies, writes one back that it encoded itself,
// and finally decodes the other board's iBeacon from the advertisement alone.
//
// The codec headers in the two repositories are verbatim copies, which is exactly
// why this scenario exists: two copies that drifted apart would still pass their
// own unit tests, and only a round trip across the air shows it.
//
// Output is prefixed ESPBLE_ so a log line never leaves it ambiguous which stack
// produced it.

#include <EspBle.h>
#include <EspBleCgmCrc.h>
#include <EspBleIBeacon.h>
#include <EspBleMedicalFloat.h>

// Interop UUIDs live in the 01xx suite-tag range (tests/TEST_PLAN.md).
static constexpr const char *SERVICE_UUID =
  "01050000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *TEMPERATURE_UUID =
  "01050001-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *GLUCOSE_UUID =
  "01050002-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *WRITTEN_UUID =
  "01050003-b1dd-4d00-9e5a-627564726f69";

// 9.87 mmol/L as an SFLOAT (mantissa 987, exponent -2), encoded here and decoded
// on the other board.
static constexpr int16_t WRITE_MANTISSA = 987;
static constexpr int8_t WRITE_EXPONENT = -2;

EspBle ble;
bool ready = false;
bool connectionRequested = false;
bool wantBeacon = false;
bool beaconReported = false;
EspBleConnectionId connectionId = 0;

void reportValue(const char *label, const String &value)
{
  // Every profile value is reported as its raw bytes *and* as the decode, so a
  // mismatch says whether the bytes or the codec is at fault.
  Serial.printf("%s length=%u hex=", label,
    static_cast<unsigned>(value.length()));
  for (size_t index = 0; index < value.length(); ++index)
  {
    Serial.printf("%02x", static_cast<uint8_t>(value[index]));
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  ble.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    Serial.printf("ESPBLE_CONNECTED id=%u\n",
      static_cast<unsigned>(connection.id));
  });
  ble.onServicesDiscovered([](const EspBleGattResult &result) {
    Serial.printf("ESPBLE_DISCOVERY success=%u characteristics=%u\n",
      result.success ? 1 : 0,
      static_cast<unsigned>(
        ble.discoveredCharacteristicCount(result.connectionId)));
  });
  ble.onCharacteristicRead([](const EspBleGattResult &result) {
    const uint8_t *data = reinterpret_cast<const uint8_t *>(result.value.c_str());
    const size_t length = result.value.length();
    if (result.characteristicUuid.equalsIgnoreCase(TEMPERATURE_UUID))
    {
      reportValue("ESPBLE_TEMPERATURE", result.value);
      Serial.printf(" flags=%02x milli=%ld\n", length > 0 ? data[0] : 0,
        length >= 5 ? lround(espBleReadMedicalFloat32LE(data + 1) * 1000.0) : 0);
      return;
    }
    if (result.characteristicUuid.equalsIgnoreCase(GLUCOSE_UUID))
    {
      reportValue("ESPBLE_GLUCOSE", result.value);
      // The CRC is checked with this library's copy of the CGM codec over the
      // bytes the other copy produced; then the payload itself is decoded.
      Serial.printf(" crc=%u milli=%ld\n",
        espBleCgmVerifyCrc(data, length) ? 1 : 0,
        length >= 4 ? lround(espBleReadMedicalSFloatLE(data + 2) * 1000.0) : 0);
      return;
    }
    reportValue("ESPBLE_READ", result.value);
    Serial.println();
  });
  ble.onCharacteristicWritten([](const EspBleGattResult &result) {
    Serial.printf("ESPBLE_WRITE success=%u\n", result.success ? 1 : 0);
  });
  ble.onNotification([](const EspBleGattNotification &notification) {
    const uint8_t *data =
      reinterpret_cast<const uint8_t *>(notification.value.c_str());
    reportValue("ESPBLE_NOTIFICATION", notification.value);
    Serial.printf(" milli=%ld\n",
      notification.value.length() >= 5
        ? lround(espBleReadMedicalFloat32LE(data + 1) * 1000.0) : 0);
  });
  ble.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf("ESPBLE_DISCONNECTED id=%u\n",
      static_cast<unsigned>(connection.id));
    connectionRequested = false;
  });
  ble.scanner().onResult([](const EspBleScanResult &result) {
    if (!wantBeacon)
    {
      if (connectionRequested || !result.advertisesService(SERVICE_UUID)) return;
      connectionRequested = true;
      ble.scanner().stop();
      Serial.printf("ESPBLE_TARGET_FOUND address=%s\n", result.address.c_str());
      Serial.printf("ESPBLE_CONNECT_REQUESTED %u\n",
        ble.connect(result, 10000) ? 1 : 0);
      return;
    }
    const uint8_t *manufacturer =
      reinterpret_cast<const uint8_t *>(result.manufacturerData.c_str());
    EspBleIBeaconData beacon;
    if (!beaconReported &&
        espBleDecodeIBeacon(
          manufacturer, result.manufacturerData.length(), beacon))
    {
      // The beacon is identified by the UUID the other board encoded, so another
      // beacon in the room cannot satisfy this scan.
      char uuid[33];
      for (size_t index = 0; index < 16; ++index)
      {
        snprintf(uuid + index * 2, 3, "%02x", beacon.uuid[index]);
      }
      if (strcmp(uuid, "01050100b1dd4d009e5a627564726f69") != 0)
      {
        return;
      }
      beaconReported = true;
      ble.scanner().stop();
      Serial.printf("ESPBLE_BEACON uuid=%s major=%04x minor=%04x power=%d\n",
        uuid, beacon.major, beacon.minor, beacon.measuredPower);
    }
  });

  EspBleConfig config;
  config.deviceName = "EspBle Profile Wire";
  if (!ble.begin(config))
  {
    Serial.printf("ESPBLE_BEGIN_FAILED %s %s\n", ble.lastErrorName(),
      ble.lastErrorDetail().c_str());
    return;
  }
  ready = true;
  Serial.println("ESPBLE_PROFILE_WIRE_READY");
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
      Serial.printf("ESPBLE_PROFILE_WIRE_STATE ready=%u\n", ready ? 1 : 0);
    }
    else if (command == 'c')
    {
      wantBeacon = false;
      EspBleScanConfig scanConfig;
      scanConfig.active = true;
      Serial.printf("ESPBLE_SCAN_STARTED %u\n",
        ble.scanner().start(scanConfig) ? 1 : 0);
    }
    else if (command == 'd')
    {
      Serial.printf("ESPBLE_DISCOVERY_REQUESTED %u\n",
        ble.discoverServices(connectionId, 5000) ? 1 : 0);
    }
    else if (command == 't')
    {
      Serial.printf("ESPBLE_TEMPERATURE_READ_REQUESTED %u\n",
        ble.readCharacteristic(
          connectionId, SERVICE_UUID, TEMPERATURE_UUID, 5000) ? 1 : 0);
    }
    else if (command == 'g')
    {
      Serial.printf("ESPBLE_GLUCOSE_READ_REQUESTED %u\n",
        ble.readCharacteristic(
          connectionId, SERVICE_UUID, GLUCOSE_UUID, 5000) ? 1 : 0);
    }
    else if (command == 's')
    {
      Serial.printf("ESPBLE_SUBSCRIBE_REQUESTED %u\n",
        ble.subscribe(connectionId, SERVICE_UUID, TEMPERATURE_UUID, true, 5000)
          ? 1 : 0);
    }
    else if (command == 'w')
    {
      // Encoded here with the released EspBle copy of the codec; the other board
      // decodes it with this repository's copy.
      uint8_t value[3];
      value[0] = 0x02; // mmol/L
      espBleWriteMedicalSFloatLE(value + 1, WRITE_MANTISSA, WRITE_EXPONENT);
      Serial.printf("ESPBLE_WRITE_REQUESTED %u\n",
        ble.writeCharacteristic(connectionId, SERVICE_UUID, WRITTEN_UUID, value,
          sizeof(value), true, 5000) ? 1 : 0);
    }
    else if (command == 'x')
    {
      Serial.printf("ESPBLE_DISCONNECT_REQUESTED %u\n",
        ble.disconnect(connectionId) ? 1 : 0);
    }
    else if (command == 'b')
    {
      wantBeacon = true;
      beaconReported = false;
      EspBleScanConfig scanConfig;
      scanConfig.active = true;
      Serial.printf("ESPBLE_BEACON_SCAN_STARTED %u\n",
        ble.scanner().start(scanConfig) ? 1 : 0);
    }
  }
  delay(1);
}
