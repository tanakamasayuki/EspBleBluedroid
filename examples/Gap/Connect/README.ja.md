# Connect

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 2章「GAP編 — 探してつながる」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

特定のService UUIDをadvertiseするPeripheralを探し、Centralとして接続します。非同期の接続モデルを示すexampleです: `connect()`は要求の受理だけを返し、完了（または失敗）は後から`bluetooth.update()`経由のイベントとして届きます。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（Central）
- 対象Service UUIDをadvertiseするBLE Peripheral × 1 — 例えば2台目のボードで[Gatt/Basics/Server](../../Gatt/Basics/Server/) exampleを動かします（`TARGET_SERVICE_UUID`を合わせて変更してください）

## 動作

- active scanを開始し、各resultから`TARGET_SERVICE_UUID`を探します
- 最初に一致した相手へscanを停止して接続を要求します
- 接続・切断・接続失敗のイベントをlibrary connection IDと一緒に表示します
- 切断・失敗後は次のscan resultで再試行します

sketch冒頭の`TARGET_SERVICE_UUID`を、接続したいPeripheralがadvertiseするUUIDへ書き換えてください。

## 主なAPI

- `scanResult.advertisesService(uuid)` — 16-bit表記（`"1812"`）と128-bit表記のどちらでも一致します
- `bluetooth.connect(scanResult)` — 要求を受理して即座に返ります。接続処理自体は内部taskで実行されます
  - `bluetooth.connect(scanResult, timeoutMilliseconds)` — timeoutは`update()`から強制されます（既定10000ms）
- `bluetooth.connect(address, EspBleAddressType, timeoutMilliseconds)` — 保存済みaddressからScanなしで接続します
- `bluetooth.onConnected(callback)` / `bluetooth.onDisconnected(callback)` — どちらも同じ安定した`connection.id`を持ちます
- `bluetooth.onConnectionFailed(callback)` — 非同期失敗。`failure.detail`で理由を確認できます

## 注意

- **Central接続は同時に1つだけです。** active linkがある状態での2回目の`connect()`は`InvalidState`で拒否されます。先に切断してください。
- **Connection IDは1回の`begin()`〜`end()` lifecycle内でのみ有効です。** `end()`はactive linkと未配送eventを破棄し、`onDisconnected()`は呼びません。再初期化後はIDを再利用することがあります。
- **接続試行中の`end()`は同期的に戻りますが、最大約1秒かかることがあります。** 待機を1秒以下の区間に分けているため呼び出し自体は戻りますが、中断した試行のcallbackは配送されません。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| 同時に張れるCentral接続 | 複数 | **1つ** |
| `connect()`のモデル | 非同期、結果は`update()`から | 同じ |
| 理由指定の`disconnect(id, reason)` overload | あり | **なし** |

**なぜ違うのか:** 直接GATTCバックエンドへの移行が進行中で、実機のpeer testで固定しているのが1 linkであるため、公開APIはCentral接続を1つに保っています（[docs/STATUS.ja.md](../../../docs/STATUS.ja.md)参照）。理由指定の`disconnect()`が無いのは、Arduino-ESP32 3.3.11のBluedroid Clientが呼び出し側の指定するlocal reasonをlink終了へ渡さないためです。引数を黙って無視するoverloadを公開しない判断です。相手側から届いたHCIの切断理由は、`onDisconnected()`の`connection.disconnectReason`で従来どおり参照できます。

**移植のしかた:** 接続は1台ずつ行い、`disconnect(id)`は理由を付けずに呼びます。

## 期待されるSerial出力

```
Connected to 5a:b8:1e:0c:2f:71 (id=1)
Disconnected (id=1)
```
