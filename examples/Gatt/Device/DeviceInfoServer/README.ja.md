# DeviceInfoServer

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../../DIFFERENCES_FROM_ESPBLE.ja.md)

標準Device Information Service（`0x180A`）でManufacturer Name（`0x2A29`）、Model Number（`0x2A24`）、Firmware Revision（`0x2A26`）、PnP ID（`0x2A50`）を公開するPeripheralです。

## 必要なもの

- 1 × 無印ESP32（このスケッチ。Peripheral）
- 1 × Central: [DeviceInfoClient](../DeviceInfoClient/) example、または任意のBLE Central/スマートフォン

## 動作

- `begin()`の前に4つのread-only Characteristicを登録し、`0x180A`をAdvertise
- Manufacturer = "EspBleBluedroid"、Model = "DeviceInfoServer"、Firmware = ライブラリのバージョン文字列を設定
- PnP IDは標準の7-byte形式でエンコード: Vendor ID Source `0x02`（USB-IF）+ little-endianのVendor ID `0x1234`、Product ID `0x5678`、Product Version `0x0100`

## 主なAPI

- `bluetooth.gattServer().addCharacteristic(..., { .readable = true })` — 各read-only Characteristicを宣言
- `bluetooth.gattServer().setValue(...)` — 文字列・生バイト値を設定

## メモ

- PnP IDは固定7-byteのバイナリで、マルチバイトのフィールドはwire上でlittle-endianです。

## 期待されるSerial出力

```
Device Information Service is ready.
```
