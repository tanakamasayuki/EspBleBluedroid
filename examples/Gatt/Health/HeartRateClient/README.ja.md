# HeartRateClient

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../../DIFFERENCES_FROM_ESPBLE.ja.md)

標準Heart Rate Service（0x180D）をAdvertiseするPeripheralへ接続し、Body Sensor LocationをReadして、Heart Rate MeasurementのNotificationを購読します。

## 必要なもの

- 1 × 無印ESP32（このスケッチ。Central）
- 1 × Heart Rate Peripheral: [HeartRateServer](../HeartRateServer/) example、または市販の心拍計

## 動作

- 0x180DをAdvertiseする機器をscanして接続
- Body Sensor Location（0x2A38）をRead
- Heart Rate Measurement（0x2A37）のNotificationを購読
- Measurementをflagsに従ってデコード: 8/16-bit心拍数、任意のEnergy Expended、複数のRR-Intervalを、可変長の境界を検証しながら処理

## 主なAPI

- `bluetooth.readCharacteristic(connectionId, service, characteristic)` — Body Sensor LocationをRead
- `bluetooth.subscribe(connectionId, service, characteristic)` — Notificationを購読
- `bluetooth.onNotification(...)` — 各Heart Rate Measurementを受信

## 期待されるSerial出力

```
Body Sensor Location: 1
Heart Rate subscription: ready
Heart Rate: 71 bpm, RR intervals: 1 (first: 1024/1024 s)
```
