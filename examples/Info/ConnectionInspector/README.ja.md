# ConnectionInspector

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 2章「GAP編 — 探してつながる」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

対話式の診断ツールです。周囲のconnectableな機器を番号つきで一覧表示し、選んだ相手へ接続してConnection snapshotをすべてダンプします: connection ID、backend handle、peer addressと種別、local role、交換済みMTU（とNotification payload上限）、security状態（encrypted / authenticated / bonded / 鍵長）。Bond storeと診断カウンタのダンプもできます。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（Central）
- 調べたい周囲のBLE Peripheral（advertise中の任意の機器）

## 動作

- scanして最大10台のconnectableな機器を`[index] address rssi name`形式で一覧します
- `0`〜`9`でその番号の機器へ接続し、Connection snapshotを表示します
- `s`で一覧をクリアして再scan、`d`で現在の接続を切断、`b`でBond storeをダンプ、`q`で診断カウンタを表示します
- security無効なので、暗号化必須のPeripheralでも接続自体は成立してlink情報を確認できます（attributeへのアクセスは拒否されます）

## 主なAPI

- `EspBleConnection` — `id`、`handle`、`peerAddress`、`peerAddressType`、`localRole`、`mtu`、`maximumNotificationPayload()`、`encrypted`、`authenticated`、`bonded`、`encryptionKeySize`
- `bluetooth.connect(scanResult)` / `bluetooth.disconnect(connectionId)` / `bluetooth.onConnectionFailed(callback)`
- `bluetooth.bondCount()` / `bluetooth.bond(index, out)` — Bond storeのsnapshotアクセス
- `bluetooth.connectionCount()`、`bluetooth.droppedEventCount()`、`bluetooth.scanner().droppedResultCount()`

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| `connectionCount()`／`connection()`で列挙されるlink | 着信linkを含むすべて | **Central linkのみ** — この機器が`connect()`で開いたlink |
| 同時接続数 | 複数。一覧が複数件になりうる | 同時1つ |
| `connection.localRole` | `Central`または`Peripheral` | 常に`Central` |
| `droppedEventCount()` | event queueのoverflowカウンタ | 同じ |

**なぜ違うのか:** Peripheral connection snapshotをまだ公開していないため（[docs/STATUS.ja.md](../../../docs/STATUS.ja.md)参照）、このinspectorが報告するのは自分が開いたlinkだけです。着信linkを調べたい場合は、相手のボードでinspectorを動かし、そちらから接続してください。

**移植のしかた:** コード変更は不要です。一覧が2件以上になることと、`Peripheral` roleが現れることを期待しないでください。

## 期待されるSerial出力

```
Commands: 0-9 connect to listed device, s rescan, d disconnect, b bonds, q counters
SCAN restart success=1 - send the list number to connect
[0] 5a:b8:1e:0c:2f:71 rssi=-52 name=EspBleBluedroid Keyboard
CONNECT [0] 5a:b8:1e:0c:2f:71 accepted=1
CONNECTION id=1 handle=0 peer=5a:b8:1e:0c:2f:71(type=0) role=Central
  mtu=255 maxNotificationPayload=252
  encrypted=0 authenticated=0 bonded=0 keySize=0
```
