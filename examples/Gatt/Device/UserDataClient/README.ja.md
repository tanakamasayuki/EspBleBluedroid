# UserDataClient

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../../DIFFERENCES_FROM_ESPBLE.ja.md)

User Data Service（0x181C）へ接続し、Database Change IncrementのNotificationを購読、AgeをRead、新しいFirst NameとAgeをWriteします。書き込むたびにincrementが増え、Notificationとして届きます。

## 必要なもの

- 1 × 無印ESP32（このスケッチ。Central）
- 1 × User Data Peripheral: [UserDataServer](../UserDataServer/) example

## 動作

- 0x181CをAdvertiseする機器をscanして接続
- Database Change Increment（0x2A99）の**Notification**を購読
- Age（0x2A80）をReadし、First Name（0x2A8A）= "Ada" と Age = 42 を**応答ありWrite**
- Serverがincrementを増やすたびに、そのNotificationを表示

## 主なAPI

- `bluetooth.writeCharacteristic(connectionId, service, characteristic, data, length, true)` — 応答ありWrite

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| GATT操作の同時実行 | 複数を続けて発行できる | **1接続につき同時1操作**。2つ目は`InvalidState`で拒否される |

**なぜ違うのか:** 直接GATTCバックエンドへの移行中で、GATT Clientは現在も同時1操作です（[docs/STATUS.ja.md](../../../../docs/STATUS.ja.md)参照）。

**移植のしかた:** Writeを連鎖させます。このexampleは`onCharacteristicRead()`でFirst Nameを書き、`onCharacteristicWritten()`でAgeを書きます。1つのcallbackで両方を発行しません。

## 期待されるSerial出力

```
Age: 25
Database Change Increment: 1
Database Change Increment: 2
```
