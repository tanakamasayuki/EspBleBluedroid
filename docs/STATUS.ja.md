# 実装状況

この文書は公開APIの現在地を示す。将来設計は
[API設計方針](API_DESIGN_POLICY.ja.md)、実装手順は
[開発方針](DEVELOPMENT.ja.md)を参照する。

## 実装済み

| 領域 | 公開面 | 確認内容 |
|---|---|---|
| Lifecycle | `begin()` / `end()` / `update()` / `initialized()` | 初期化前操作拒否、同一設定の再実行、接続試行・active linkの終了 |
| Error | `lastError()` / `lastErrorName()` / `lastErrorDetail()` | state・argument・backend・resource・unsupportedの分類 |
| Advertising | name、service UUID、manufacturer data、appearance、scan response、connectable、interval、開始・停止 | raw PDU、複数UUIDの集約、31 byte境界、時間停止を実機確認 |
| Scan | active/passive、interval/window、duration、duplicate指定、開始・停止 | 値型copy、duration・明示停止、16件queue・overflow、`end()`時flushを確認 |
| Event配送 | `EspBleScanner::onResult()` | stack callbackからqueueへcopyし、利用者callbackを`update()`から配送 |
| Central接続 | `connect()` / `disconnect()` / connection snapshot / lifecycle callback | non-blocking要求、再接続ID、timeout分類、切断、active link終了、再初期化 |
| GATT Client | Database Discovery / UUID・handle指定Characteristic操作 / Descriptor Read・Write / Notification | connection単位snapshot、binary-safe値、CCCD、専用task、`update()`配送 |
| BLE Security | Just Works / Static・Runtime Passkey / Numeric Comparison / Bond | 暗号化・認証必須attribute、保存bond再接続、passkey表示・入力・比較確認、bond管理 |
| Capability | `capabilities()` | BLE、Classic、dual-mode、Classic Inquiry、未実装SPPを初期化前に判定 |
| Classic Inquiry | `classic().inquiry()` | name、address、Class of Device、RSSI、明示停止、完了event、`update()`配送 |
| Classic SPP Server | `classic().spp().startServer()` / session / write / disconnect | binary-safe双方向data、remote切断、再接続ID、稼働中`end()`、`update()`配送 |
| Classic SPP Client | `classic().spp().connect()` / connection failure / 共通session API | non-blocking SDP/RFCOMM接続、binary data、local切断、再接続ID、timeout |
| BLE/SPP dual mode | active BLE Scan + active SPP session | Scan callbackからSPP write、binary応答、独立queue、停止・切断 |

AdvertisingとScanの基本経路は`tests/peer/advertise_scan`、Advertising wire形式と
payload境界は`tests/peer/advertise_payload`で実機確認している。Scanはduration停止、
明示停止、16件queueへ18件を決定的に注入した16件配送・2件drop、未配送結果を残した
`end()`と再初期化も確認している。
Central接続は`tests/peer/connect_disconnect`でlink確立とcallback配送を分離し、切断後の
再Advertising・再Scan・再接続、新しいID、Advertising停止peerへの厳密なtimeout、
接続試行中と接続成立後の`end()`、peer切断、再初期化まで確認している。
Classic Inquiryは`tests/peer/classic_inquiry`でBTDM初期化、discoverableなClassic peer、
結果callback内からの停止、完了eventまで確認している。
SPP Serverは`tests/peer/spp_server`でraw ESP-IDF Clientとの双方向binary data、
2回の接続で異なるsession ID、remote切断、server稼働中の終了まで確認している。
SPP Clientは`tests/peer/spp_client`でraw ESP-IDF Serverとの非同期接続、双方向data、
公開APIからの切断、再接続ID、Server停止後の失敗/timeout eventまで確認している。
dual modeは`tests/peer/dual_mode_scan_spp`でSPP session中のactive BLE Scanと、
Scan Result callbackから開始するSPP binary往復を確認している。
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
- LE Secure Connections Just Works、DisplayOnly/KeyboardOnlyの静的passkey MITM、
  KeyboardOnlyの実行時Passkey Entry、DisplayYesNoのNumeric Comparisonに対応。
  実行時入力は`providePasskey()`、比較確認は`confirmNumericComparison()`で行い、
  Bluedroid callbackの待機上限はいずれも30秒。test-only seamで短縮し、両方の
  未回答timeoutが認証失敗として配送されることを実機確認している。
- Passkey Entry待機中の`disconnect()`と`end()`は入力待ちをcancelして即時終了する。
  Numeric Comparisonの明示拒否は認証を失敗させるが、BluedroidはBLE linkを自動では
  切断しないため、applicationが必要に応じて`disconnect()`する。
- Arduino-ESP32 BLE wrapperはprocess内のpasskey設定を解除できない。このため、同一bootで
  静的またはDisplayOnlyのpasskey設定を使って`end()`した後、KeyboardOnlyの実行時入力へ
  構成変更する場合は再起動が必要。通常の同一構成での再初期化には影響しない。
- Central接続は同時1接続。Peripheral connectionの公開snapshotはGATT Server追加時に実装する。
- Connection IDは1回の`begin()`〜`end()` lifecycle内だけで有効。`end()`はactive linkと
  未配送eventを破棄し、利用者の`onDisconnected()`は配送しない。再初期化後はIDを再利用
  することがある。
- Bluedroidの接続待機を1秒以下の区間に分けるため、接続試行中の`end()`は同期的に
  終了するが、復帰まで最大約1秒待つことがある。終了した試行のcallbackは配送しない。
- GATT ClientはDatabase Discovery、Characteristic/Descriptor Read/Write、Subscribe/Unsubscribe。
  CharacteristicはUUID指定とhandle指定に対応する。同時1操作。
- Discovery snapshot上限はService 16、Characteristic 48、Descriptor 48。
  PSRAMは使用せずDiscovery時だけheapへ確保し、切断時に無効化する。
- GATT timeoutの結果配送には`update()`が必要。timeout後の遅いbackend完了は配送しないが、
  Bluedroid wrapperの同期ATT待機自体は応答または切断までworker task内に残るため、
  その間は次のGATT操作を受理しない。
- Classic Inquiry result queueは16件。overflowは`droppedResultCount()`で確認できる。
  Inquiry時間は1〜61秒、`maxResponses=0`はbackend上限まで探索する。Classic Inquiryは
  BLE Scanとは別の操作・結果型であり、同時実行の保証はdual-modeテスト追加後に確定する。
- SPPはClient/Server、pendingまたはactive session 1つ、pending write 1つ、
  1 writeあたり1〜990 byteに対応。ClientはSDPの先頭SPP serviceを利用する。
  複数session、Security、送信完了callback、高帯域receive bufferは未実装。
- BLE ScanとSPP session/dataの同時利用は確認済み。BLE GATT接続・ATT trafficとSPPの
  同時利用、長時間・高負荷時のfairnessは未確認。
- GATT Server、HIDおよびSPP以外のClassic profileは公開API未実装。
- Advertisingの時間指定停止は`update()`で処理するため、継続的な`update()`呼出しが必要。

## 次のテストスライス

1. SPP Securityと送受信queue。
2. BLE GATT/SPP dual-modeと長時間traffic。

各項目は失敗するunitまたはpeerテストを先に追加してから実装する。
