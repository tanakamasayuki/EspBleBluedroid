# Mtu

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 2章「GAP編 — 探してつながる」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

接続前に大きめのATT MTUを希望値として設定し、交換結果を観察します。希望MTUは`begin()`へ渡すconfigで指定し、linkが確立した直後にライブラリが交換します。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（Central）
- BLE Peripheral × 1 — このsketchは[Gatt/Basics/NotifyServer](../../Gatt/Basics/NotifyServer/) exampleのService UUIDをscanするので、2台目のボードでNotifyServerを動かしてください

## 動作

- `begin()`前に`config.preferredMtu = 185`を設定します
- NotifyServerのService UUIDをadvertiseする最初の相手へ接続します
- 交換されたMTUと、そこから決まるNotification payload上限（`mtu - 3`）を表示します
- MTU変更イベントを変更前後の値と一緒に表示します

## 主なAPI

- `EspBleConfig::preferredMtu` — 希望ATT MTU（23〜517）。範囲外は`begin()`が`InvalidArgument`で拒否します
- `connection.mtu` — そのイベント時点のMTU。**接続直後は23**です。MTUの交換は接続が成立した直後に行われるので、`onConnected`の時点ではまだ既定値で、交換結果は`onMtuChanged`で届きます（CentralとPeripheralのどちらも同じ順序です）
- `connection.maximumNotificationPayload()` — `mtu - 3`（ATT notification header分を除いた値）
- `bluetooth.onMtuChanged(callback)` — `event.previousMtu`と`event.connection.mtu`

## メモ

- **交換はstack callbackの中ではなく`update()`から開始します。** Bluedroidは自身の接続callbackの中からのMTU要求を受け付けないため、ライブラリは接続workerの完了後、次の`update()`で1回だけ要求します。接続後に`update()`を呼ばなくなったsketchはMTU 23のままになります。
- 合意値は`onMtuChanged()`の配送と同時にconnection snapshotへ書き込まれるので、以降の`bluetooth.connection(id, out)`は交換後のMTUを返します。
- 交換中の`disconnect()`は受理され、交換の完了まで内部で遅延します。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| `EspBleConfig::preferredMtu`の既定 | 247 | 247 |
| 交換を開始する時点 | 接続確立処理の中（backend内部） | 接続worker完了後の最初の`update()` |
| `onConnected()`時点のMTU | 23（既定） | 23（既定） |

**なぜ違うのか:** Bluedroidは自身のGATTC callback contextから`esp_ble_gattc_send_mtu_req()`を呼ぶと失敗するため、linkが成立したその瞬間に要求を出せません。EspBleBluedroidはlinkごとにちょうど1件の要求をqueueし、`update()`から発行します。

**移植のしかた:** コード変更は不要です。sketchはEspBle版と同じで、`onMtuChanged()`の到達が`update()` 1周期分だけ後ろにずれるだけです。

## 期待されるSerial出力

```
Connected with MTU 23 (notification payload up to 20 bytes)
MTU changed from 23 to 185
```
