# CompileSmoke

> English: [README.md](README.md)
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../DIFFERENCES_FROM_ESPBLE.ja.md)

最小のビルド確認用sketchです。`EspBleBluedroid.h`をincludeし、共通のGAP APIを一通り呼んで、ライブラリのバージョンを表示します。Bluetooth stackは初期化しません。機能系のexampleを試す前に、対象ボードでライブラリがコンパイル・リンクできることを確認する用途に使います。

## 必要なもの

- 無印ESP32 × 1（Bluetooth Classicを持つESP32 SoC。Arduino-ESP32のBluedroid backendでビルドします）。peerは不要

## 動作

- Advertisingを開始せずに、2面のpayload（name、Service UUID、Service Data、Appearance、Tx Power）を構成します
- 値型`EspBleScanResult`のaccessorを呼び、そのsignatureをビルド対象に含めます
- `EspBluedroidSppSerial`を構築し、`connected()`を表示します
- 起動時にEspBleBluedroidのライブラリバージョンをSerialへ1回表示します

## 主なAPI

- `ESPBLEBLUEDROID_VERSION_STR` — `espblebluedroid_version.h`（`tools/bump_version.py`が生成）で定義されるバージョン文字列
- `advertising().data()` / `scanResponse()` — 独立した2面のLegacy payload
- `EspBluedroidSppSerial` — Bluetooth Classic SPPをArduinoの`Stream`として扱うwrapper

## ビルド

```sh
arduino-cli compile --profile esp32 examples/CompileSmoke
```

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| バージョンマクロ | `ESPBLE_VERSION_STR`（`espble_version.h`） | `ESPBLEBLUEDROID_VERSION_STR`（`espblebluedroid_version.h`） |
| `sketch.yaml`のprofile | `esp32s3`（既定）、`esp32c3`、`esp32c6`、`esp32h2`、`esp32p4` | `esp32`のみ |
| 追加で触るAPI | — | `EspBluedroidSppSerial`（Bluetooth Classic） |

**なぜ違うのか:** 2つのライブラリはそれぞれ独立してバージョンを持つため、マクロ名とヘッダ名も別になっています。またEspBleBluedroidは無印ESP32専用です。ESP32ファミリでBluetooth Classicを持つのは無印ESP32だけであり、それがEspBleと別にこのライブラリが存在する理由そのものです。したがって、ここのすべての`sketch.yaml`にS3/C3/C6/H2/P4のprofileはありません。

**移植のしかた:** `ESPBLE_VERSION_STR`を`ESPBLEBLUEDROID_VERSION_STR`へ置き換え、`--profile esp32`でビルドします。

## 期待されるSerial出力

```
EspBleBluedroid 0.1.0
SPP Serial connected=0
```
