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
| Capability | `capabilities()` | BLE、Classic、dual-mode、Classic Inquiry、SPPを初期化前に判定 |
| Classic Inquiry | `classic().inquiry()` | name、address、Class of Device、RSSI、明示停止、完了event、`update()`配送 |
| Classic SPP Server | `classic().spp().startServer()` / session / read / write / disconnect | binary-safe双方向data、固定長RX ring、送信完了、remote切断、再接続ID、稼働中`end()` |
| Classic SPP Client | `classic().spp().connect()` / connection failure / 共通session API | non-blocking SDP/RFCOMM接続、共通RX ring、送信完了、local切断、再接続ID、timeout |
| Classic SPP Serial | `EspBluedroidSppSerial` | rootへbindしてServer/Clientのactive sessionへ自動追従、`Stream`/`Print`、`readBytes()`、`flush()` |
| Classic Security | Numeric Comparison / Passkey Entry / SPP security mode / `classic().bond*()` | DisplayOnly/KeyboardOnly、比較値、明示回答、Client/Server認証失敗後retry、bond再接続・列挙・削除、認証・暗号化SPP data |
| BLE/SPP dual mode | active BLE Scan・GATT Client + active SPP session | Discovery、Read/Write、Notification、64→128→256通知の段階的bounded burst、round別drop集計、配送済み通知のSPP往復、GATT完了優先配送、停止・切断 |

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
2回の接続で異なるsession ID、remote切断、server稼働中の終了に加え、8件送信queueの
順序とoverflow、および受理された8件の送信完了を`update()` contextで確認している。
SPP Clientは`tests/peer/spp_client`でraw ESP-IDF Serverとの非同期接続、双方向data、
送信完了、公開APIからの切断、再接続ID、Server停止後の失敗/timeout eventまで確認している。
SPP Securityは`tests/peer/spp_security`でraw ESP-IDF peerとのDisplayYesNo SSP、
両端6桁値の一致、明示拒否、認証失敗後の再探索・再試行、両端accept後の認証・暗号化
sessionとbinary dataをClient/Server両roleで確認している。さらにClassic bondを
BLE bondとは別の公開APIで列挙・削除し、保存link keyによる確認UIなしのsecure再接続も
確認している。
Classic Passkeyは`tests/peer/spp_passkey`でpublic KeyboardOnlyとraw DisplayOnly、
およびpublic DisplayOnlyとraw KeyboardOnlyの両方向を確認している。表示・入力要求は
`update()`からpeer address付きで配送し、`providePasskey(address, value)`で回答する。
未回答timeout、期限後入力の拒否、直後のretry、同一bootで`end()`後にI/O capabilityを
反転して再初期化できることに加え、入力待ち中の`end()`が2秒未満で完了して再初期化
できることも確認している。
dual modeは`tests/peer/dual_mode_scan_spp`でSPP session中のactive BLE Scan、
BLE Central接続、GATT Discovery / Characteristic Read・Write / Notificationと、
64、128、256件のNotification burstを同じ接続・購読上で段階的にSPP binary往復へ
接続している。各roundでBLE event queueから配送された通知はすべてSPP peerの受信・
応答と公開RX ringのpacket数・byte数まで一致し、SPP write/RX dropとapplication
pendingは0になる。burst中にBLE event queueが飽和した場合も、各roundの配送数と
`droppedEventCount()`増分の合計が送信数に一致し、欠落を明示的に観測する。
加えて8件のBLE connection event queueをNotificationで満杯にした状態へGATT完了を
決定的に注入し、最古のNotification 1件をdropしてGATT完了を保持・配送することを確認した。
`tests/peer/stack_smoke`は、公開API実装前のbackend成立性として接続、GATT read/write、
CCCD購読、notificationまで確認している。

## 現在の制限

- 対象はBluetooth Classicを搭載する無印ESP32系とArduino-ESP32 3.3.11。
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
  BLE Scanとは別の操作・結果型であり、InquiryとBLE Scanの同時実行は保証しない。
- SPPはClient/Server、pendingまたはactive session 1つ、送信queue 8件、
  1 writeあたり1〜990 byteに対応。ClientはSDPの先頭SPP serviceを利用する。
  queue使用量は`pendingWriteCount()`またはsession指定overload、拒否・backend送信失敗の
  累積は`droppedWriteCount()`で確認できる。backendへ開始したwriteの完了は
  `onWriteCompleted()`からsession ID・byte数・成否・error/detail付きで配送する。
  同期的なqueue受付拒否と、切断時点で未開始のqueue項目は完了eventの対象外。
  受信は`onData()`のpacket eventに加え、stack callbackで退避する2048 byteの
  固定長ringを`available()`、`peek()`、`read()`で読める。満杯時は既存byteを保持し、
  超過分を`droppedReceiveByteCount()`で確認できる。
  `EspBluedroidSppSerial sppSerial(bluetooth)`は現在の単一active sessionへ自動追従し、
  Server/Client両roleでArduino `Stream`/`Print` APIを利用できる。writeは990 byte単位へ
  分割し、`availableForWrite()`は固定長送信queueの残り容量をbyteで返す。
  同じwrapperが切断後の次sessionへ追従して再びI/Oできることを両roleで確認済み。
  ラッパーはstackやsessionを所有せず、rootより長く生存してはならない。
  SPP Security modeは認証のみ、または認証＋暗号化を選べる。Classic側は
  NoInputNoOutput、DisplayOnly、KeyboardOnly、DisplayYesNo SSPに対応し、Passkey入力と
  比較確認の未回答は既定30秒で拒否する。
  Classic bondは`EspBluedroidClassicBond`としてBLE bond storeから分離し、
  `classic().bondCount()` / `bond()` / `deleteBond()` / `deleteAllBonds()`で管理する。
  複数sessionは未実装。将来拡張時のsession別resource、fairness、自動Serialのsticky選択、
  明示session adapterとの分離は
  [API設計方針](API_DESIGN_POLICY.ja.md#spp複数session拡張境界)で定義している。
  raw Bluedroidでは異なる2 SCNを使った同一ACL上の2 sessionを実機確認済みだが、
  公開SPP APIの上限は引き続き1 session。
- BLE ScanおよびBLE GATT接続・ATT trafficとSPP session/dataの同時利用は確認済み。
  64→128→256 Notificationのbounded burstを同じ接続・購読上で段階的に実行し、
  roundごとにBLE event queueのdropを含む全件を集計して配送済み通知のSPP往復と
  RX ring保持を確認している。BLE connection event queue満杯時はNotificationより
  接続・Security・GATT完了などの制御eventを優先する。長時間soakとround境界なしの
  連続飽和状態でのfairnessは未確認。
- GATT Server、HIDおよびSPP以外のClassic profileは公開API未実装。
- Advertisingの時間指定停止は`update()`で処理するため、継続的な`update()`呼出しが必要。

## 次のテストスライス

1. BLE GATT/SPP dual-modeの長時間soakとround境界なしの連続飽和時fairness。
2. 異なるSCNを使うincoming Serverとoutgoing Clientの同時sessionを2台fixtureで検証する。
   公開実装は、session別RX/write/fairness/cleanupを失敗するテストとして先に固定できた
   場合だけ開始する。同じServer serviceへの複数client試験には3台目のpeerが必要。

各項目は失敗するunitまたはpeerテストを先に追加してから実装する。
