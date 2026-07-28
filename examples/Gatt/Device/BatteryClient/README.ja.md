# BatteryClient

> English: [README.md](README.md)

標準Battery Service（`0x180F`）をscanし、Battery Level（`0x2A19`）をReadしてから、
Notificationを購読するCentralです。

## 必要なもの

- 無印ESP32 × 1（このsketch。Central）
- 標準Battery ServiceをAdvertisingし、Battery LevelのReadとNotificationに対応するPeripheral

## 動作

- Active Scanで`0x180F`をAdvertisingする最初の機器に接続します
- 1 byteのBattery LevelをReadして表示します
- Read成功後、Battery LevelのNotificationを購読します
- 以降のレベル変更を受信して表示します

## 主なAPI

- `scanner().onResult()` / `advertisesService()` — 接続対象を選択
- `readCharacteristic()` / `onCharacteristicRead()` — 現在値を取得
- `subscribe()` / `onSubscribed()` — Notificationを有効化
- `onNotification()` — 変更値を受信

各要求の戻り値は受付結果です。Readや購読の完了は`update()`から後で配送されます。

## 期待されるSerial出力

```text
Battery: 75%
Battery subscription: ready
Battery changed: 76%
```
