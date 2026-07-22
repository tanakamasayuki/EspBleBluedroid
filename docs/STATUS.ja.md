# 実装状況

この文書は公開APIの現在地を示す。将来設計は
[API設計方針](API_DESIGN_POLICY.ja.md)、実装手順は
[開発方針](DEVELOPMENT.ja.md)を参照する。

## 実装済み

| 領域 | 公開面 | 確認内容 |
|---|---|---|
| Lifecycle | `begin()` / `end()` / `update()` / `initialized()` | 初期化前の操作拒否、同一設定での`begin()`再実行 |
| Error | `lastError()` / `lastErrorName()` / `lastErrorDetail()` | state・argument・backend・resource・unsupportedの分類 |
| Advertising | name、service UUID、manufacturer data、appearance、scan response、connectable、interval、開始・停止 | raw PDU、複数UUIDの集約、31 byte境界、時間停止を実機確認 |
| Scan | active/passive、interval/window、duration、duplicate指定、開始・停止 | name、address、RSSI、manufacturer data、service UUID、接続可能性を値型へcopy |
| Event配送 | `EspBleScanner::onResult()` | stack callbackからqueueへcopyし、利用者callbackを`update()`から配送 |

AdvertisingとScanの基本経路は`tests/peer/advertise_scan`、Advertising wire形式と
payload境界は`tests/peer/advertise_payload`で実機確認している。
`tests/peer/stack_smoke`は、公開API実装前のbackend成立性として接続、GATT read/write、
CCCD購読、notificationまで確認している。

## 現在の制限

- 対象はBluetooth Classicを搭載する無印ESP32系とArduino-ESP32 3.3.10。
- 必須機能はPSRAMなしで動作する設計とし、build確認はgeneric `esp32` profileに集約する。
  PSRAM搭載moduleなど、同じESP32 SoC内のboard variant別matrixは作らない。
- Legacy Advertisingのみ。Extended Advertisingには未対応。
- Advertising service UUIDは格納上限4、Scan Resultは格納上限8。超過したScan UUIDの
  個数はまだ個別に報告しない。
- Scan result queueは16件。overflowは`droppedResultCount()`で確認できる。
- Securityを有効にした`begin()`は`EspBleError::Unsupported`で失敗する。
- 接続、GATT Client/Server、Security、Bond、HID、Bluetooth Classicは公開API未実装。
- Advertisingの時間指定停止は`update()`で処理するため、継続的な`update()`呼出しが必要。

## 次のテストスライス

1. Scanの停止、duration、queue overflow、`end()`後の再初期化。
2. BLE接続と安定したlibrary connection ID。
3. GATT discovery、read/write、subscribeをEspBleに近い非同期APIで公開。
4. BLE Securityを実機で確定。
5. Classic Inquiry、SPP、BLE/SPP dual-modeの順に追加。

各項目は失敗するunitまたはpeerテストを先に追加してから実装する。
