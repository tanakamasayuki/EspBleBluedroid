# 実装状況

この文書は公開APIの現在地を示す。将来設計は
[API設計方針](API_DESIGN_POLICY.ja.md)、実装手順は
[開発方針](DEVELOPMENT.ja.md)を参照する。

## 実装済み

| 領域 | 公開面 | 確認内容 |
|---|---|---|
| Lifecycle | `begin()` / `end()` / `update()` / `initialized()` | 初期化前操作拒否、同一設定の再実行、接続試行・active linkの終了 |
| Error | `lastError()` / `lastErrorName()` / `lastErrorDetail()` | state・argument・backend・resource・unsupportedの分類 |
| Advertising | `data()` / `scanResponse()`、name、service UUID/data、manufacturer data、appearance、Tx Power、connectable、interval、開始・停止 | 2面の独立構成、raw PDU、複数UUIDの集約、31 byte境界、時間停止を実機確認 |
| iBeacon | `EspBleIBeacon.h` encode / decode | EspBle共通codecのunit testとnon-connectable broadcast/scanを実機確認 |
| UUID codec | `EspBleUuid.h` parse / format / compare | EspBle共通codecで16/32/128-bit、短縮形とBluetooth Base UUIDの等価性をunit test |
| Scan | active/passive、interval/window、duration、duplicate指定、accept list filter、rich result、開始・停止 | AdvertisingとScan Responseのaddress単位merge、Service Data・Appearance・Tx Power、値型copy、controller filter、duration・明示停止、16件queue・overflow、`end()`時flushを確認 |
| Event配送 | `EspBleScanner::onResult()` | stack callbackからqueueへcopyし、利用者callbackを`update()`から配送 |
| Advertising identity / radio | `ownAddressType` / `localAddress()` / `setTxPower()` | Public、Random Static、RPA、−12/+9 dBmと電波上の値を確認 |
| Advertising accept list | `addToAcceptList()` / 一覧管理 / `setFilterPolicy()` | controller一覧同期、一覧外接続の拒否、`Any`へ変更後の接続・切断 |
| Directed Advertising | `setDirectedTarget()` / `start()` / High・Low Duty / peer address type | 宛先Centralでの空payload受信・接続、High Duty自動停止、Low Duty継続・明示停止 |
| Central接続 | `connect()` / `disconnect()` / connection snapshot / lifecycle・MTU・parameter callback | non-blocking要求、再接続ID、MTU交換、接続パラメータ取得・更新、HCI切断理由、timeout分類、切断、再初期化 |
| GATT Client | Database Discovery / Characteristic単体Discovery / UUID・handle指定Characteristic操作 / Descriptor Read・Write / Notification | connection単位snapshot、Characteristic・Descriptor handle、binary-safe値、CCCD、専用task、`update()`配送 |
| GATT Server | `gattServer()` / Service・Characteristic・Descriptor登録 / Read・Write / Notify・Indicate | begin前の静的定義、opaque handle、binary-safe値、動的Read、CCCD購読、Notificationを実機確認 |
| BLE Security | Just Works / Static・Runtime Passkey / Numeric Comparison / Bond | 暗号化・認証必須attribute、保存bond再接続、passkey表示・入力・比較確認、bond管理 |
| Capability | `capabilities()` | BLE、Classic、dual-mode、Classic Inquiry、SPPを初期化前に判定 |
| Classic Inquiry | `classic().inquiry()` | name、address、Class of Device、RSSI、明示停止、完了event、`update()`配送 |
| Classic SPP Server | `classic().spp().startServer()` / session / read / write / disconnect | binary-safe双方向data、固定長RX ring、送信完了、remote切断、再接続ID、稼働中`end()` |
| Classic SPP Client | `classic().spp().connect()` / connection failure / 共通session API | non-blocking SDP/RFCOMM接続、共通RX ring、送信完了、local切断、再接続ID、timeout |
| Classic SPP Serial | `EspBluedroidSppSerial` | rootへbindしてServer/Clientのactive sessionへ自動追従、`Stream`/`Print`、`readBytes()`、`flush()` |
| Classic Security | Numeric Comparison / Passkey Entry / SPP security mode / `classic().bond*()` | DisplayOnly/KeyboardOnly、比較値、明示回答、Client/Server認証失敗後retry、bond再接続・列挙・削除、認証・暗号化SPP data |
| BLE/SPP dual mode | active BLE Scan・GATT Client + active SPP session | Discovery、Read/Write、Notification、64→128→256通知の段階的bounded burst、round別drop集計、配送済み通知のSPP往復、GATT完了優先配送、停止・切断 |

AdvertisingとScanの基本経路は`tests/peer/advertise_scan`、Advertising wire形式と
payload境界は`tests/peer/advertise_payload`で実機確認している。Active Scanでは
BluedroidがAdvertisingだけを先に報告する場合があるため、短時間だけ同じaddressの
Scan Responseをmergeしてから公開callbackへ配送する。Scanはduration停止、
明示停止、16件queueへ18件を決定的に注入した16件配送・2件drop、未配送結果を残した
`end()`と再初期化も確認している。
Central接続は`tests/peer/connect_disconnect`でlink確立とcallback配送を分離し、既定の
希望MTU 247と23→185の交換event、HCI切断理由、切断後の再Advertising・再Scan・再接続、
新しいID、Advertising停止peerへの厳密なtimeout、
接続試行中と接続成立後の`end()`、peer切断、再初期化まで確認している。
接続パラメータは`tests/peer/connection_parameters`で初期interval/latency/timeoutが
connection snapshotへ入ることと、Centralからinterval 80（100ms）、latency 0、
timeout 200（2秒）を要求した後、公開callbackとraw Bluedroid Peripheralの双方が同じ
合意値を報告することを確認している。callbackは`update()` contextから配送される。
Local identityは`tests/peer/local_identity`でRandom Staticのsubtype、公開したaddressと
scanで観測した値の一致、RPAのsubtype、および−12/+9 dBmの設定値とAdvertising上の
Tx Power Levelが一致することを確認している。
Advertising Filter Accept Listは`tests/peer/accept_list`で初期化前操作、重複追加、
controllerによる一覧外Centralの接続拒否、`Any`へのpolicy変更後の接続と正常切断を
確認している。変更はAdvertising開始時にcontrollerへ同期する。
Directed Advertisingは`tests/peer/directed_advertising`でpublic addressのCentralを
宛先にしたHigh Duty packetがaddress・RSSI・connectableだけを持って届き、そのまま
接続・切断できることを確認している。未接続High Dutyは1.28秒で停止し、Low Dutyは
1.5秒後も継続して明示停止できる。
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
- Directed AdvertisingはHigh/Low Dutyに対応する。仕様上payloadとScan Responseはなく、
  activeな別Advertisingを停止してから開始する。
- Advertising own addressはPublic、Random Static、RPAに対応する。Scan Requestは現在
  Public address固定。RPAはcontroller管理で、現在値を
  返す公開GAP APIがないため`localAddress()`は空文字列を返す。
- Filter Accept Listは最大8件で、Advertising側のScan Request・接続要求と、
  Central Scan側の受信対象をcontrollerで制限できる。
- Advertising service UUID / service dataは各payloadで各4件、Scan Resultはservice UUID
  8件、service data 4件。超過したScan fieldの個数はまだ個別に報告しない。
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
- 希望MTUの既定はEspBleと同じ247。各linkは23で接続し、接続worker完了後に
  `update()`から1回だけ交換を開始する。Bluedroid callback内からは要求しない。
  交換中の`disconnect()`は要求を受理してMTU完了後まで内部で遅延する。合意値は
  `onMtuChanged()`で配送し、connection snapshotも同時に更新する。
- `onDisconnected()`のsnapshotには低レベルGATTC eventから得たHCI切断理由を格納する。
  Arduino-ESP32 3.3.11のBluedroid Clientはローカル切断理由の指定値をlink終了へ渡さない
  ため、理由指定`disconnect()` overloadは公開しない。
- Connection Interval、Peripheral Latency、Supervision Timeoutは接続時snapshotと
  `onConnectionParametersUpdated()`で取得できる。`updateConnectionParameters()`は
  Centralのactive connectionに対する要求を受け付ける。接続開始時の値指定とPHY変更は
  未実装で、無印ESP32はLE 2M/Coded PHYに対応しない。
- Bluedroidの接続待機を1秒以下の区間に分けるため、接続試行中の`end()`は同期的に
  終了するが、復帰まで最大約1秒待つことがある。終了した試行のcallbackは配送しない。
- GATT ClientはDatabase Discovery、Characteristic単体Discovery、Characteristic/Descriptor
  Read/Write、Subscribe/Unsubscribe。CharacteristicとDescriptorはUUID指定とhandle指定に
  対応する。同時1操作。
- GATT ServerはService 8、Characteristic 32、Descriptor 16までを`begin()`前に登録する。
  Bluedroid wrapperの制約により、同じService内で同一UUIDのCharacteristicは登録時に拒否する。
  Read callbackだけは応答前に値を決めるためstack task、Write・Descriptor・購読・送信完了は
  `update()`から配送する。Peripheral connection snapshot、複数observer、profile helper、
  自動再接続・再購読は未実装。
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
- HID、BLE MIDIおよびSPP以外のClassic profileは公開API未実装。
- Advertisingの時間指定停止は`update()`で処理するため、継続的な`update()`呼出しが必要。

## BLE直接バックエンド移行

Arduino BLE wrapperを撤去する作業は
[BLE直接バックエンド移行計画](BLE_DIRECT_BACKEND_MIGRATION.ja.md)に従って進めている。
第1段階のBLE address、UUID、Legacy Advertising Data codecを内部実装へ移し、
backend非依存unit testと既存Advertising / Scan peer試験を通している。

次のGATTC直接化に向けて、application登録、接続要求、cancel競合、切断、再接続、
古い操作generationの無視を表す内部状態機械とunit testを追加済みである。
GATT Database Discoveryは`esp_ble_gattc_search_service()`とGATTC Search event、
Characteristic / Descriptorのcache列挙APIへ直接移行済みである。Discovery済みsnapshotを
対象とするCharacteristic Readも`esp_ble_gattc_read_char()`と完了eventへ直接移行した。
snapshotがないReadと、Write、Descriptor、Subscribeは公開互換性を保つため現在も
wrapper fallbackを使用する。
公開接続・GATT Client経路はまだ`BLEClient` / `BLERemote*`を利用しており、
直接GATTC eventへ切り替わったとは扱わない。

## 次のテストスライス

1. Characteristic Write、Descriptor Read / Write、Subscribe / NotificationをGATTC eventへ
   直接移し、GATT worker taskと`BLERemote*`依存を撤去する。
2. GATTC application登録と直接接続をtest seamへ接続し、OPEN成功、失敗、cancel競合、
   timeout、`end()`、再接続を既存peer scenarioで固定する。
3. BLE GATT/SPP dual-modeの長時間soakとround境界なしの連続飽和時fairness。
4. 異なるSCNを使うincoming Serverとoutgoing Clientの同時sessionを2台fixtureで検証する。
   公開実装は、session別RX/write/fairness/cleanupを失敗するテストとして先に固定できた
   場合だけ開始する。同じServer serviceへの複数client試験には3台目のpeerが必要。

各項目は失敗するunitまたはpeerテストを先に追加してから実装する。
