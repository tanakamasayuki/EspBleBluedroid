# Connect

> English: [README.md](README.md)

Battery Service（`0x180F`）をAdvertisingするPeripheralを探し、非同期に接続します。

## 必要なもの

- 無印ESP32 × 1（このsketch。Central）
- Battery ServiceをAdvertisingするPeripheral

## 動作

- Active ScanでService UUIDが一致するpeerを選びます
- scanを止め、`connect(scanResult)`で接続を要求します
- 接続、非同期失敗、切断を別callbackで受け取ります
- 切断後は次のscan resultから再試行できる状態へ戻します

## 主なAPI

- `scanResult.advertisesService()` — Service UUIDでpeerを選択
- `connect()` — 戻り値は要求の受付結果
- `onConnected()` / `onConnectionFailed()` / `onDisconnected()`
- `EspBleConnection::id` / `disconnectReason` — 公開IDとHCI切断理由

現在はCentral同時1接続です。再接続時は新しいconnection IDが発行されます。
`end()`は未完了要求のcallbackを破棄し、接続待機区間の終了に最大約1秒かかる場合があります。

## 期待されるSerial出力

```text
Connected: id=1 peer=00:11:22:33:44:55 mtu=23
Disconnected: id=1
```
