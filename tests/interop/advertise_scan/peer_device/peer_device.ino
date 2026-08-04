// Cross-stack advertising / scan: this library against EspBle (NimBLE). This is
// the `peer_device` half — the board running the library under test — while the
// ESP32-S3 running EspBle is the parent fixture.
//
// Two claims are checked here that Bluedroid-to-Bluedroid cannot check, because
// both ends would make the same assumption and it would cancel out:
//
//   * an advertisement built by EspBle's payload builder is reconstructed
//     field for field by this library's scanner, including the per-address merge
//     of the advertising payload with the scan response an active scan collects;
//   * the payload this library builds is reconstructed the same way by EspBle's
//     parser, so the split across the two payloads is on the air and not just in
//     the local structure.
//
// The passive scan is what makes the merge observable: it must see the
// advertising payload's fields and none of the scan response's.
//
// Every step is driven by a serial command, so a failure names the step.

#include <EspBleBluedroid.h>

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

static constexpr const char *ADVERTISED_NAME = "Bluedroid Adv";
static constexpr uint16_t ADVERTISED_APPEARANCE = 0x0442;
static constexpr uint8_t MANUFACTURER_DATA[] = {0xe5, 0x02, 0x22, 0x02};
static constexpr uint8_t SERVICE_DATA[] = {0x32, 0x02, 0x0b};

EspBleBluedroid bluetooth;
bool ready = false;
bool reported = false;
const char *resultPrefix = "SCAN_RESULT";

void printHex(const String &value)
{
  for (size_t index = 0; index < value.length(); ++index)
  {
    Serial.printf("%02x", static_cast<uint8_t>(value[index]));
  }
}

// One line per result, in the same shape as the EspBle side prints, so the test
// compares two stacks field by field instead of two log formats. The name is
// bracketed because it contains a space — and because an empty name has to be
// distinguishable from a missing field during the passive scan.
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

bool startScan(bool active, const char *prefix)
{
  reported = false;
  resultPrefix = prefix;
  EspBleScanConfig scanConfig;
  scanConfig.active = active;
  scanConfig.intervalMilliseconds = 100;
  scanConfig.windowMilliseconds = 50;
  return bluetooth.scanner().start(scanConfig);
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (reported || !result.advertisesService(ESPBLE_SERVICE_UUID))
    {
      return;
    }
    reported = true;
    reportResult(resultPrefix, result);
    bluetooth.scanner().stop();
  });

  EspBleConfig config;
  config.deviceName = ADVERTISED_NAME;
  if (!bluetooth.begin(config))
  {
    Serial.printf("BEGIN_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }

  // The same split as the EspBle side builds: everything a passive scanner must
  // still see stays in the advertising payload, and the name, manufacturer data
  // and Service Data go to the scan response.
  auto &advertising = bluetooth.advertising();
  auto &data = advertising.data();
  if (!data.addServiceUuid(BLUEDROID_SERVICE_UUID))
  {
    Serial.printf("UUID_FAILED %s\n", bluetooth.lastErrorName());
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
    Serial.printf("SERVICE_DATA_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  advertising.setScanResponseEnabled(true);

  ready = true;
  Serial.println("INTEROP_ADVERTISE_SCAN_READY");
}

void loop()
{
  bluetooth.update();
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.printf("READY_STATE ready=%u\n", ready ? 1 : 0);
    }
    else if (command == 's')
    {
      Serial.printf("SCAN_STARTED %u\n", startScan(true, "SCAN_RESULT") ? 1 : 0);
    }
    else if (command == 'q')
    {
      Serial.printf("PASSIVE_SCAN_STARTED %u\n",
        startScan(false, "PASSIVE_RESULT") ? 1 : 0);
    }
    else if (command == 'p')
    {
      Serial.printf("ADVERTISING %u\n",
        bluetooth.advertising().start() &&
            bluetooth.advertising().isAdvertising()
          ? 1 : 0);
    }
    else if (command == 'P')
    {
      Serial.printf("ADVERTISING_STOPPED %u\n",
        bluetooth.advertising().stop() &&
            !bluetooth.advertising().isAdvertising()
          ? 1 : 0);
    }
  }
  delay(1);
}
