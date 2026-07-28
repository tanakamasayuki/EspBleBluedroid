// en: Split legacy advertising across its two independent 31-byte payloads.
// ja: Legacy Advertisingの独立した2つの31byte payloadを使い分ける例。
#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID =
  "5266f727-49d7-4eaf-a6f1-7363616e7270";
static constexpr uint16_t APPEARANCE_THERMOMETER = 0x0341;

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "Bluedroid Scan Response";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE init failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  // en: The radio rounds to a supported level. This affects both the actual
  //     transmitter and the Tx Power AD field included below.
  // ja: 無線が対応する値へ丸める。実際の送信電力と、下で追加する
  //     Tx Power AD fieldの両方へ反映される。
  if (!bluetooth.setTxPower(3))
  {
    Serial.printf("Tx Power failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  auto &advertising = bluetooth.advertising();

  // en: This side reaches passive and active scanners. Flags are automatic:
  //       flags 3 + 128-bit UUID 18 + appearance 4 + Tx Power 3 = 28 bytes.
  // ja: この面はpassive/active双方へ届く。Flagsは自動付与され、
  //     3 + 128bit UUID 18 + Appearance 4 + Tx Power 3 = 28byte。
  auto &data = advertising.data();
  data.addServiceUuid(SERVICE_UUID);
  data.setAppearance(APPEARANCE_THERMOMETER);
  data.setTxPowerIncluded(true);

  // en: This separate 31-byte side reaches active scanners only.
  // ja: こちらは別枠の31byteで、active scannerだけへ届く。
  const uint8_t manufacturerData[] = {0xff, 0xff, 0x01, 0x02};
  auto &scanResponse = advertising.scanResponse();
  scanResponse.setName("Bluedroid Response");
  scanResponse.setManufacturerData(
    manufacturerData, sizeof(manufacturerData));

  if (!advertising.start())
  {
    Serial.printf("Advertising failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }
  Serial.printf("Advertising with an explicit scan response at %d dBm\n",
    static_cast<int>(bluetooth.txPower()));
}

void loop()
{
  bluetooth.update();
  delay(1);
}
