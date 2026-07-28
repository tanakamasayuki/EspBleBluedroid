# Mtu

> English: [README.md](README.md)

希望ATT MTUを設定し、新しいlinkの初期値から合意値への変更を観察します。

## 必要なもの

- 無印ESP32 × 1（このsketch。Central）
- Battery ServiceをAdvertisingし、MTU 185以上を受理するPeripheral

## 動作

- `config.preferredMtu = 185`を指定します
- `onConnected()`で新しいATT linkの初期MTU 23を表示します
- `onMtuChanged()`で合意MTUとNotification payload上限を表示します
- 切断時はHCI切断理由を表示します

## 主なAPI

- `EspBleConfig::preferredMtu` — 希望値（23〜517、既定247）
- `EspBleMtuChanged::previousMtu` / `connection.mtu`
- `EspBleConnection::maximumNotificationPayload()` — `mtu - 3`
- `onMtuChanged()` — `update()`から届く交換完了callback

## メモ

希望値と相手側上限の小さい方が採用されます。交換開始とbackend eventの処理は
EspBleBluedroidが行うため、applicationは希望値と完了callbackだけを扱います。

## 期待されるSerial出力

```text
Connected with initial MTU 23
MTU changed from 23 to 185 (notification payload up to 182 bytes)
```
