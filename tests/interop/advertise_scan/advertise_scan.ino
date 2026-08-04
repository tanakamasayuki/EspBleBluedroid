// The EspBle (NimBLE) half of the cross-stack advertising / scan scenario,
// running on an ESP32-S3 against the released EspBle pinned in sketch.yaml.
//
// This board plays both roles, one at a time: it advertises a payload built with
// EspBle's builder for the Bluedroid scanner to reconstruct, and it scans for the
// payload the Bluedroid board builds. Nothing here is interop-specific — it is an
// ordinary EspBle advertiser and scanner, so a field that does not survive the
// trip is a difference between the two stacks and not a quirk of the test.
//
// Output is prefixed ESPBLE_ so a log line never leaves it ambiguous which stack
// produced it.

#include <EspBle.h>

// Interop UUIDs live in the 01xx suite-tag range (tests/TEST_PLAN.md). Each side
// advertises its own so that neither scanner can be satisfied by the other's
// payload, or by a suite running on nearby hardware.
static constexpr const char *ESPBLE_SERVICE_UUID =
  "01010000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *BLUEDROID_SERVICE_UUID =
  "01010001-b1dd-4d00-9e5a-627564726f69";
// Service Data is carried under a 16-bit UUID: the 31-byte payload has no room
// for a second 128-bit one, and the block is only ever looked up inside a result
// that already matched the 128-bit service UUID above.
static constexpr const char *SERVICE_DATA_UUID = "180f";

// What this board advertises. The values are arbitrary but distinct from the
// other board's, so a mixed-up direction cannot pass.
static constexpr const char *ADVERTISED_NAME = "EspBle Adv";
static constexpr uint16_t ADVERTISED_APPEARANCE = 0x03c1;
static constexpr uint8_t MANUFACTURER_DATA[] = {0xe5, 0x02, 0x11, 0x01};
static constexpr uint8_t SERVICE_DATA[] = {0x64, 0x01, 0x0a};

EspBle ble;
bool ready = false;
bool reported = false;

void printHex(const String &value)
{
  for (size_t index = 0; index < value.length(); ++index)
  {
    Serial.printf("%02x", static_cast<uint8_t>(value[index]));
  }
}

// One line per result, in the same shape as the Bluedroid side prints, so the
// test compares two stacks field by field instead of two log formats. The name
// is bracketed because it contains a space.
void reportResult(const char *prefix, const EspBleScanResult &result)
{
  String serviceData;
  const bool hasServiceData =
    result.serviceDataFor(SERVICE_DATA_UUID, serviceData);
  // The block's own UUID as the parser reports it: both libraries promise the
  // full 128-bit form even for a 16-bit advertisement, so it is compared too.
  const String serviceDataUuid =
    result.serviceDataCount > 0 ? result.serviceData[0].uuid : String("none");

  Serial.printf("%s name=[%s] mfg=", prefix, result.name.c_str());
  printHex(result.manufacturerData);
  Serial.printf(" sd_uuid=%s sd=", serviceDataUuid.c_str());
  if (hasServiceData)
  {
    printHex(serviceData);
  }
  Serial.printf(
    " appearance=%04x tx_present=%u connectable=%u scannable=%u\n",
    result.appearance,
    result.hasTxPowerLevel() ? 1 : 0,
    result.connectable ? 1 : 0,
    result.scannable ? 1 : 0);
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  ble.scanner().onResult([](const EspBleScanResult &result) {
    if (reported || !result.advertisesService(BLUEDROID_SERVICE_UUID))
    {
      return;
    }
    reported = true;
    reportResult("ESPBLE_SCAN_RESULT", result);
    ble.scanner().stop();
  });

  EspBleConfig config;
  config.deviceName = ADVERTISED_NAME;
  if (!ble.begin(config))
  {
    Serial.printf("ESPBLE_BEGIN_FAILED %s %s\n", ble.lastErrorName(),
      ble.lastErrorDetail().c_str());
    return;
  }

  // The advertising payload holds the fields a passive scanner must still see;
  // the name, manufacturer data and Service Data go to the scan response, which
  // is what makes the other side's active-scan merge observable.
  auto &advertising = ble.advertising();
  auto &data = advertising.data();
  if (!data.addServiceUuid(ESPBLE_SERVICE_UUID))
  {
    Serial.printf("ESPBLE_UUID_FAILED %s\n", ble.lastErrorName());
    return;
  }
  data.setAppearance(ADVERTISED_APPEARANCE);
  data.setTxPowerIncluded(true);

  auto &scanResponse = advertising.scanResponse();
  scanResponse.setName(ADVERTISED_NAME);
  scanResponse.setManufacturerData(MANUFACTURER_DATA, sizeof(MANUFACTURER_DATA));
  if (!scanResponse.addServiceData(
        SERVICE_DATA_UUID, SERVICE_DATA, sizeof(SERVICE_DATA)))
  {
    Serial.printf("ESPBLE_SERVICE_DATA_FAILED %s\n", ble.lastErrorName());
    return;
  }
  advertising.setScanResponseEnabled(true);

  ready = true;
  Serial.println("ESPBLE_READY");
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
      Serial.printf("ESPBLE_READY_STATE ready=%u\n", ready ? 1 : 0);
    }
    else if (command == 'a')
    {
      Serial.printf("ESPBLE_ADVERTISING %u\n",
        ble.advertising().start() && ble.advertising().isAdvertising() ? 1 : 0);
    }
    else if (command == 'A')
    {
      Serial.printf("ESPBLE_ADVERTISING_STOPPED %u\n",
        ble.advertising().stop() && !ble.advertising().isAdvertising() ? 1 : 0);
    }
    else if (command == 's')
    {
      reported = false;
      EspBleScanConfig scanConfig;
      scanConfig.active = true;
      scanConfig.intervalMilliseconds = 100;
      scanConfig.windowMilliseconds = 50;
      Serial.printf("ESPBLE_SCAN_STARTED %u\n",
        ble.scanner().start(scanConfig) ? 1 : 0);
    }
  }
  delay(1);
}
