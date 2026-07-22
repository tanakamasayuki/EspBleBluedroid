# 実装状況

この文書は公開APIの現在地を示す。将来設計は
[API設計方針](API_DESIGN_POLICY.ja.md)、実装手順は
[開発方針](DEVELOPMENT.ja.md)を参照する。

## 実装済み

| 領域 | 公開面 | 確認内容 |
|---|---|---|
| Lifecycle | `begin()` / `end()` / `update()` / `initialized()` | 初期化前の操作拒否、同一設定での`begin()`再実行、接続試行の終了 |
| Error | `lastError()` / `lastErrorName()` / `lastErrorDetail()` | state・argument・backend・resource・unsupportedの分類 |
| Advertising | name、service UUID、manufacturer data、appearance、scan response、connectable、interval、開始・停止 | raw PDU、複数UUIDの集約、31 byte境界、時間停止を実機確認 |
| Scan | active/passive、interval/window、duration、duplicate指定、開始・停止 | 値型へのcopy、duration・明示停止、`end()`時の未配送queue破棄を実機確認 |
| Event配送 | `EspBleScanner::onResult()` | stack callbackからqueueへcopyし、利用者callbackを`update()`から配送 |
| Central接続 | `connect()` / `disconnect()` / connection snapshot / lifecycle callback | non-blocking要求、再接続ごとの新ID、二重要求・不正address拒否、非同期失敗、切断、再初期化 |
| GATT Client | Database Discovery / Read / Write / Subscribe / Unsubscribe / Notification | connection単位snapshot、binary-safe値、CCCD、専用task、`update()`配送 |

AdvertisingとScanの基本経路は`tests/peer/advertise_scan`、Advertising wire形式と
payload境界は`tests/peer/advertise_payload`で実機確認している。Scanはduration停止、
明示停止、未配送結果を残した`end()`と再初期化も確認している。
Central接続は`tests/peer/connect_disconnect`でlink確立とcallback配送を分離し、切断後の
再Advertising・再Scan・再接続、新しいID、到達不能peerの非同期失敗、接続試行中の
`end()`と再初期化まで確認している。
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
- Central接続は同時1接続。Peripheral connectionの公開snapshotはGATT Server追加時に実装する。
- Bluedroidの接続待機を1秒以下の区間に分けるため、接続試行中の`end()`は同期的に
  終了するが、復帰まで最大約1秒待つことがある。終了した試行のcallbackは配送しない。
- GATT ClientはDatabase Discovery、Characteristic Read/Write、Subscribe/Unsubscribe。
  同時1操作で、Descriptor Read/Writeとhandle指定操作は未実装。
- Discovery snapshot上限はService 16、Characteristic 48、Descriptor 48。
  PSRAMは使用せずDiscovery時だけheapへ確保し、切断時に無効化する。
- GATT timeoutの結果配送には`update()`が必要。timeout後の遅いbackend完了は配送しないが、
  Bluedroid wrapperの同期ATT待機自体は応答または切断までworker task内に残るため、
  その間は次のGATT操作を受理しない。
- GATT Server、Security、Bond、HID、Bluetooth Classicは公開API未実装。
- Advertisingの時間指定停止は`update()`で処理するため、継続的な`update()`呼出しが必要。

## 次のテストスライス

1. Scan queue overflowとdrop countを電波頻度に依存せず決定的に確認するtest seam。
2. 接続timeoutの厳密な分類、接続成立後の`end()`。
3. GATT Descriptor Read/Writeとhandle指定操作をEspBleに近い非同期APIで公開。
4. BLE Securityを実機で確定。
5. Classic Inquiry、SPP、BLE/SPP dual-modeの順に追加。

各項目は失敗するunitまたはpeerテストを先に追加してから実装する。
