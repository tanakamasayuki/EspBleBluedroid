# Mtu

> English: [README.md](README.md)

Battery Service（`180F`）をAdvertisingするPeripheralへ接続し、ATT MTUの交換を
観察します。希望値は`begin()`へ渡す`EspBleConfig`で指定します。

## 動作

- `config.preferredMtu = 185`を設定して初期化します
- Battery Serviceを見つけるとscanを止め、非同期に接続します
- `onConnected()`で新しいATT linkの初期MTU 23を表示します
- 交換完了後、`onMtuChanged()`で変更前後のMTUとNotification payload上限を表示します
- 切断時はHCI切断理由を表示します

`preferredMtu`は要求値です。相手の最大値が小さければ、小さい方が合意値になります。
MTUにはATT headerも含まれるため、Notificationでapplicationが1回に運べる上限は
`maximumNotificationPayload()`が返す`mtu - 3`です。

## 主なAPI

- `EspBleConfig::preferredMtu` — 希望ATT MTU（23〜517、既定247）
- `EspBleMtuChanged::previousMtu` — 交換前のMTU
- `EspBleMtuChanged::connection.mtu` — 交換後のconnection snapshot
- `EspBleConnection::maximumNotificationPayload()` — MTUからATT header 3 byteを除いた値
- `onMtuChanged()` — MTU交換完了を`update()`から受け取るcallback

希望値185に対応するPeripheralへ接続した場合の出力例です。

```text
Connected with initial MTU 23
MTU changed from 23 to 185 (notification payload up to 182 bytes)
```
