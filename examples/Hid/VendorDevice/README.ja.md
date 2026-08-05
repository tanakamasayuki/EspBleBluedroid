# VendorDevice

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

vendor定義のHIDデバイスです。任意サイズのInput・Output・Feature Reportを持ち、中身のバイト列はライブラリが解釈しません。固定profileと違い双方向なので、HostからDeviceへ書き込めます。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（HIDデバイス / Peripheral）
- vendor Reportを書き込めるHID Host × 1: [VendorHost](../VendorHost/)を動かす2枚目のボード、またはHostアプリ。`tests/peer/hid_vendor_custom`がまさにこの形を駆動しています

## 動作

- `begin()`の前に、Reportサイズ8 byteでprofileを構成します
- MTUを100へ上げます。ATT payloadは MTU − 3 のためです
- Hostが書き込んだOutput・Feature Reportをすべて表示します
- Serialコマンド`i`で8 byteのInput Reportを送ります

## Report 3件、Report ID 1つ

このprofileはReport ID 6のもとで、UUID `0x2A4D`を共有する3つのCharacteristicを公開します。区別はReport Reference descriptorのtype byteです: Input（notify）、Output（writeまたはwrite without response）、Feature（write with responseのみ。設定情報なのでHost側が応答を求める）。

## 主なAPI

- `bluetooth.hidVendor().configure(config)` — profileを登録。`bluetooth.begin()`より**前**に呼ぶ
- `config.reportSize` — 1〜64 byte。Report Descriptorへ埋め込まれる
- `vendor.sendInput(data, length)` — Input Report 1件。`length`は`reportSize`と一致する必要がある
- `vendor.onOutputReport(cb)` / `vendor.onFeatureReport(cb)` — Hostが書き込んだ内容。`update()`から配送される
- `vendor.ready()` — Hostが接続し、（securityが有効なら）暗号化され、購読済み

## 注意

- **宣言したサイズが唯一のサイズです。** descriptorが固定するため、`sendInput()`は他の長さをpaddingも切り詰めもせず拒否します。
- **20 byteを超えるReportにはMTUの引き上げが必要です。** ATT payloadは MTU − 3 なので、既定のMTU 23では20 byteを超えた分が落ちます。両側の合意が必要で、`config.preferredMtu`はこのデバイス側の希望値にすぎません。
- **`data`/`length`と`rawData`/`rawLength`は同じバイト列です。** 解釈を持つprofileが前者に解釈結果を入れても元のReportが隠れないように、両方が用意されています。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| クラス名・メソッド名・callback名 | `ble.hidVendor()` | 同一 |
| Report Descriptor | 自前の表から生成 | 同一のバイト列（`tests/unit/hid_report_maps`が比較） |
| Output・Feature Reportのcallback | stack callback内でキューされ、次の`update()`で配送 | GATT Serverが元々writeを`update()`から配送するため、同じ`update()`で配送 |

**移植方法:** ライブラリオブジェクトの宣言を変えるだけです。

## 期待されるSerial出力

```
Send 'i' to send an 8-byte Vendor Input Report.
Input: sent
Output type=2 length=8 data=01 02 03 04 05 06 07 08
```
