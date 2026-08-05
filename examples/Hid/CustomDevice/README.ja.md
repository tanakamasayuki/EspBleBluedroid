# CustomDevice

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

`bluetooth.hidCustom()`で**任意の**Report DescriptorのHIDデバイスを作ります。この例はvendor定義の「コントロールパネル」で、2 byteの入力Report（符号付きダイヤル差分＋ボタンbit）と1 byteの出力Report（HostがLED状態を書き込む）を持ちます。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（HIDデバイス / Peripheral）
- このdescriptorを解釈するHID Host × 1: Hostアプリ、または汎用GATT Clientとして振る舞う2枚目のボード。`tests/peer/hid_vendor_custom`がまさにこの形のデバイスを駆動しています

## 動作

- `configure()`→Reportごとに`addInputReport()` / `addOutputReport()`→`setReportMap()`。すべて`begin()`の前に行います
- Hostが1 byteの出力Reportを書き込むたびに表示します
- Serialコマンド`i`で2 byteの入力Report（ダイヤル+5、ボタン1）を送ります

## descriptorは利用者のもの、配線はライブラリのもの

ライブラリは`setReportMap()`へ渡したバイト列をReport Map（`0x2A4B`）として公開し、宣言した各Reportに専用の`0x2A4D` characteristicとReport Reference（`{report id, type}`）を与えます。**行わない**のは両者の整合チェックです。descriptorはHost OSとの契約なので、宣言するReport IDとサイズはdescriptorが記述しているものと一致させてください。

カスタムReportは固定profileと同じHID Serviceへ合成されるので、`hidKeyboard()` / `hidMouse()`と共存できます。その場合のReport Mapは、合成された内蔵descriptorのあとに利用者のdescriptorが続く形になります。Report ID 1〜6が予約されるのは、対応する内蔵profileを有効にしている場合だけです。

## 主なAPI

- `bluetooth.hidCustom().configure()` — HID Serviceを立ち上げる。`bluetooth.begin()`より**前**に呼ぶ
- `custom.setReportMap(bytes, length)` — raw Report Descriptor。最大256 byte
- `custom.addInputReport(id, size)` / `addOutputReport()` / `addFeatureReport()` — 合計で`EspBleHidCustom::MaxReports`（4）まで
- `custom.sendInput(id, data, length)` — `length`は宣言したサイズと一致する必要がある
- `custom.onOutputReport(cb)` / `onFeatureReport(cb)` — Hostが書き込んだ内容。`update()`から配送される
- `custom.ready(id)` — Input Reportごと。Hostは各Reportを別々に購読するため

## 注意

- **宣言は`begin()`より前に。** 各Reportは属性になり、属性テーブルは1度だけ構築されます。
- **宣言していないReport IDはReportではありません。** `sendInput()`はcharacteristicを勝手に作らず`NotFound`で失敗します。
- **20 byteを超えるReportにはMTUの引き上げ（`config.preferredMtu`）が必要です。** ATT payloadは MTU − 3 のためです。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| クラス名・メソッド名・callback名 | `ble.hidCustom()` | 同一 |
| Report Mapの合成 | 内蔵descriptorのあとにカスタムdescriptor | 同一 |
| `MaxReports` | 4 | 4 |
| Output・Feature Reportのcallback | stack callback内でキューされ、次の`update()`で配送 | GATT Serverが元々writeを`update()`から配送するため、同じ`update()`で配送 |

**移植方法:** ライブラリオブジェクトの宣言を変えるだけです。

## 期待されるSerial出力

```
Send 'i' to send an input report (dial +5, button 1).
Output report id=1 len=1 value=2
```
