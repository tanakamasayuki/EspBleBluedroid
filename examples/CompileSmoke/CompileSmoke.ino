// en: CompileSmoke - minimal sketch that only checks the library builds and links
//     on an original ESP32 with the Bluedroid backend. It does not initialize the
//     Bluetooth stack; it touches the common EspBle-compatible GAP surface so a
//     signature change breaks the build here, and prints the library version.
// ja: CompileSmoke - 無印ESP32のBluedroid構成でライブラリがbuild・linkできることだけを
//     確認する最小sketch。Bluetooth stackは初期化しない。EspBle互換のGAP APIを一通り
//     触ることでsignature変更をこのbuildで検出し、ライブラリのバージョンを表示する。
#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;
// en: The SPP Stream wrapper is Bluedroid-only; constructing it keeps it in the
//     compile matrix. It never owns the stack, so this is safe without begin().
// ja: SPPのStream wrapperはBluedroid固有。ここで構築してcompile matrixに含める。
//     stackを所有しないため、begin() なしでも安全。
EspBluedroidSppSerial sppSerial(bluetooth);

void setup()
{
  Serial.begin(115200);

  // en: Configure both advertising payloads without starting anything. These are
  //     the same calls the Gap examples use.
  // ja: 何も開始せずにAdvertisingの2面を構成する。Gapのexampleと同じ呼び出し。
  auto &advertising = bluetooth.advertising();
  advertising.data().setName("EspBleBluedroid");
  advertising.data().addServiceUuid("180f");
  const uint8_t serviceData[] = {0x64};
  advertising.data().addServiceData(
    "180f", serviceData, sizeof(serviceData));
  advertising.data().setAppearance(0x0341);
  advertising.data().setTxPowerIncluded(true);
  advertising.scanResponse().setName("EspBleBluedroid");

  // en: Value-type scan result accessors, again only for the signature check.
  // ja: 値型のScan result accessor。これもsignature確認のためだけに呼ぶ。
  EspBleScanResult result;
  String value;
  (void)result.hasServiceData();
  (void)result.hasAppearance();
  (void)result.hasTxPowerLevel();
  (void)result.serviceDataFor("180f", value);

  // en: ESPBLEBLUEDROID_VERSION_STR comes from espblebluedroid_version.h, which
  //     tools/bump_version.py generates.
  // ja: ESPBLEBLUEDROID_VERSION_STR は tools/bump_version.py が生成する
  //     espblebluedroid_version.h で定義される。
  Serial.printf("EspBleBluedroid %s\n", ESPBLEBLUEDROID_VERSION_STR);
  Serial.printf("SPP Serial connected=%u\n", sppSerial.connected() ? 1 : 0);
}

void loop()
{
  // en: This sketch does nothing; the goal is just to verify the build.
  // ja: このsketchは何もしない（ビルド確認が目的）。
  delay(1000);
}
