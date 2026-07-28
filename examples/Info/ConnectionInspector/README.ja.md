# ConnectionInspector

> English: [README.md](README.md)

周囲のconnectableなBLE機器を一覧し、番号を指定して接続し、connection snapshot、
bond一覧、drop counterを表示する対話式診断exampleです。

## 必要なもの

- 無印ESP32 × 1
- 周囲のconnectableなBLE Peripheral

## 動作

- 最大10件を`[番号] address rssi name`形式で一覧します
- `0`〜`9`で接続し、初期snapshotとMTU交換後snapshotを表示します
- `s`で再scan、`d`で切断、`b`でbond一覧、`q`でcounterを表示します

## 主なAPI

- `EspBleConnection` — id、handle、peer、role、MTU、Security状態
- `onMtuChanged()` — MTU交換後の更新済みsnapshot
- `bondCount()` / `bond()` — BLE bond storeの列挙
- `droppedEventCount()` / `scanner().droppedResultCount()` — queue診断

Securityは有効にしていないため、暗号化必須のAttribute操作は対象外です。
