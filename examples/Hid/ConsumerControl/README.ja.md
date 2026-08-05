# ConsumerControl

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

キーボードのメディアキーを単体のHIDデバイスにします。1 ReportにConsumer pageのusageを1つ（16 bit）だけ載せる形式で、音量・再生/一時停止・曲送りがHost OSへ届きます。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（HIDデバイス / Peripheral）
- HID Host × 1: PC、スマホ、タブレット。OSのBluetooth設定からPairingします

## 動作

- `begin()`の前に`bluetooth.hidConsumerControl().configure()`を呼びます
- Serialコマンド`+`で音量アップ、`-`で音量ダウン、`p`で再生/一時停止します

## 主なAPI

- `bluetooth.hidConsumerControl().configure()` — profileを登録。`bluetooth.begin()`より**前**に呼ぶ
- `media.click(usage)` — usage 1つを押して離す
- `media.press(usage)` / `release()` / `usage()` — 保持中のusage（同時に1つ）
- `ESP_BLE_HID_CONSUMER_CONTROL_VOLUME_UP`など — descriptorが対象とするusage

## 注意

- **1 Reportにusageは1つです。** descriptorは16 bitのusageフィールドを1つだけ宣言するので、このprofileが表現できるのは同時に1キーです。`press()`は保持中のusageに追加するのではなく置き換えます。
- **release Reportが重要です。** usage 0を送ることがキーを離した合図になります。送らないとHost側が動作を繰り返す場合があります。`click()`は両方を行います。
- **範囲内のusageならそのまま渡せます。** 定数は代表的なものだけで全部ではありません。descriptorはConsumer pageを対象にしているので、HID usage tablesのusageを直接指定できます。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| クラス名・メソッド名・callback名 | `ble.hidConsumerControl()` | 同一 |
| Report Descriptor | 自前の表から生成 | 同一のバイト列（`tests/unit/hid_report_maps`が比較） |

**移植方法:** ライブラリオブジェクトの宣言を変えるだけです。

## 期待されるSerial出力

```
Send '+', '-', or 'p'.
```
