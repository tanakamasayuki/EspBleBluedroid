# Changelog / 変更履歴

## Unreleased
- (EN) Add `tests/TEST_PLAN.md` / `.ja.md`: the EspBle test-plan structure plus the
  Classic and interop layers, a documented test-UUID allocation, coverage tables,
  and the priority order for the remaining gaps.
- (JA) `tests/TEST_PLAN.md`・`.ja.md`を追加。EspBleのテスト計画構造にClassicとinterop層、
  テスト用UUIDの割り当て、カバレッジ表、残り空白の優先順位を加えた。
- (EN) Add the EspBle cross-stack suite `tests/interop`: the EspBle side runs on an
  ESP32-S3 with the released version pinned in `sketch.yaml`, and
  `interop/gatt_basic` drives a Bluedroid central against an EspBle (NimBLE)
  peripheral through MTU exchange, discovery, read, both write modes, descriptor
  access, notification, and confirmed indication. The S3 is part of the standard
  fixture, so a bare `pytest` covers it.
- (JA) EspBleとのcross-stack suite `tests/interop`を追加。EspBle側はESP32-S3で動かし、
  対象releaseは`sketch.yaml`でversion固定する。`interop/gatt_basic`がBluedroid Centralと
  EspBle（NimBLE）Peripheralの間でMTU交換、Discovery、Read、2種のWrite、Descriptor操作、
  Notification、確認応答付きIndicationを検証する。S3は標準fixtureの一部なので、無指定の
  `pytest`にも含まれる。実機で確認済み（MTU 247交換、Service 3件・
  Characteristic handle 16のDiscovery、Read/Write、Descriptor、Notification、Indication、
  購読解除、切断）。
- (EN) Add `interop/advertise_scan`: an advertising payload and scan response built
  by one stack's builder, reconstructed field for field by the other's parser, in
  both directions — device name, manufacturer data, Service Data (with the 16-bit
  UUID reported in its 128-bit form), Appearance and Tx Power. A passive scan of
  the same advertiser must see the advertising payload's fields and nothing from
  the scan response, which is what pins the scanner's per-address merge to two
  payloads actually received rather than one reported twice. Verified on hardware.
- (JA) `interop/advertise_scan`を追加。一方のbuilderで組んだAdvertising payloadと
  Scan Responseを、相手stackのparserが同じfieldへ復元することを両方向で検証する
  （device name、manufacturer data、Service Data（16-bit UUIDが128-bit形で報告されること
  を含む）、Appearance、Tx Power）。同じadvertiserをpassive scanしたときはAdvertising
  payload側のfieldだけが見え、Scan Response側は見えないことも固定した。これによりScanner
  のaddress単位mergeが「実際に受信した2つのpayloadの合成」であることを担保する。実機で
  確認済み。
- (EN) Add `interop/long_value`: a 300-byte value published by an EspBle (NimBLE)
  peripheral is read whole across the 247-byte MTU, through both the UUID form and
  the handle form, with every byte checked against the peer's ramp. `peer/long_value`
  has Bluedroid on both ends, so this is what makes the no-truncation claim about
  the client rather than about the pair. Verified on hardware.
- (JA) `interop/long_value`を追加。EspBle（NimBLE）Peripheralが公開した300 byteの値を、
  MTU 247をまたいでUUID指定・handle指定の両方のReadで全体取得できることを、全byteを相手の
  ramp列と照合して確認する。`peer/long_value`は両端がBluedroidなので、「切り詰めない」ことを
  client側の性質として言えるのはこちら。実機で確認済み。
- (EN) Add `interop/duplicate_uuid`: an EspBle (NimBLE) peripheral publishing two
  characteristics that share a UUID inside one service — the shape this library's
  server API rejects, and the shape its client half must still consume. Discovery
  keeps both apart, the UUID form reaches the first, and the reads, the write, the
  subscription and the notification are each attributed to a handle on both sides.
  Verified on hardware.
- (JA) `interop/duplicate_uuid`を追加。EspBle（NimBLE）Peripheralが同一Service内に置いた
  同一UUID Characteristic 2件——本ライブラリのServer APIが拒否する形であり、Client側は
  扱えなければならない形——に対して、Discoveryが2件を区別し、UUID指定は1件目に届き、
  Read、Write、購読、Notificationがすべて両側でhandleに帰属することを確認する。実機で確認済み。
- (EN) Add `interop/security`: cross-stack pairing against an EspBle (NimBLE)
  peripheral, twice over — Just Works, then static-passkey Passkey Entry
  (DisplayOnly against KeyboardOnly, which is what selects an *authenticated*
  link; two DisplayOnly sides fall back to Just Works). Encrypted, authenticated,
  bonded and the 16-byte key size are asserted on both sides, both record the
  bond, and two characteristics carry the encrypted and authenticated permission
  tiers so what each link may reach is observed: the authenticated one is refused
  on the Just Works link and readable/writable on the Passkey Entry link. Bonds
  are cleared from NVS first, since a leftover bond would let a run pass without
  pairing. Verified on hardware.
- (JA) `interop/security`を追加。EspBle（NimBLE）Peripheral相手のcross-stack pairingを
  2種類——Just Worksと静的passkeyのPasskey Entry（DisplayOnly対KeyboardOnly。これが
  **authenticated** linkを選ぶ組み合わせで、両側DisplayOnlyではJust Worksに落ちる）——で
  検証する。encrypted / authenticated / bonded / key size 16を両側でassertし、bondも両側で
  確認。encryptedとauthenticatedの権限2段をCharacteristicで表現し、Just Works linkでは
  authenticated側が拒否され、Passkey Entry linkではRead/Writeできることを観測する。
  NVSのbondは事前に消す（残っていると pairingせずに通ってしまう）。実機で確認済み。
- (EN) Extend `interop/security` with Numeric Comparison, confirmed and refused.
  Both hosts derive the six digits independently, so the test asserts they are
  **equal** — a cross-stack mismatch would ask a user comparing two screens to
  accept something the stacks disagree on — and that confirming yields an
  authenticated, bonded link. Refusing on the library-under-test side must leave
  the link unencrypted and unbonded on *both* sides, so a local "no" cannot end
  with the peer believing the user confirmed. Verified on hardware.
- (JA) `interop/security`にNumeric Comparison（承認と拒否）を追加。6桁の数字は両host
  それぞれが導出するため、**一致すること**をassertする（食い違えば、2つの画面を見比べる
  利用者にstack間で不一致な値の承認を求めることになる）。承認時はauthenticated + bondedに
  なること、被検ライブラリ側が拒否した場合は**両側**でlinkが暗号化されずbondも残らないこと
  （ローカルの「いいえ」が相手側では承認扱い、という終わり方をしないこと）を確認する。
  実機で確認済み。
- (EN) `lastErrorName()` now returns EspBle's spelling: `NONE`,
  `INVALID_ARGUMENT`, `INVALID_STATE`, … instead of `None`, `InvalidArgument`,
  `InvalidState`. **Behaviour change** for code that logs or compares the string —
  the `EspBleError` enum constants themselves are unchanged. The two libraries had
  the same signature and different strings, so a sketch moved between them printed
  something different; nothing in the backend forced that. `UNSUPPORTED` has no
  EspBle counterpart because the enum constant exists only here (Classic profiles).
- (JA) `lastErrorName()`の戻り値をEspBleの綴りに統一。`None`・`InvalidArgument`・
  `InvalidState`などを`NONE`・`INVALID_ARGUMENT`・`INVALID_STATE`へ変更した。この文字列を
  ログや比較に使っているコードには**挙動変更**（`EspBleError`のenum定数自体は変更なし）。
  signatureは同じで文字列だけが違ったため、sketchを移すと表示が変わっていた。backend制約に
  よる差ではない。`UNSUPPORTED`はこちらにしか存在しないenum定数（Classic profile用）のため
  EspBleに対応がない。
- (EN) `tests/unit/api_parity` now also compares what the `*Name()` functions
  return, not only that they exist: the enum-to-string maps in
  `src/EspBleBluedroid.cpp` against the new `espble.values` snapshot, with every
  difference classified in `docs/API_PARITY.tsv` like a missing symbol. The target
  functions are found by shape, so a name map added later is covered without
  editing the test. `tools/gen_api_parity.py` gained `--espble-source`.
- (JA) `tests/unit/api_parity`が`*Name()`関数の**戻り値**も比較するようになった。
  `src/EspBleBluedroid.cpp`のenum→文字列対応を新しい`espble.values` snapshotと突き合わせ、
  差分はシンボル差と同じく`docs/API_PARITY.tsv`で分類する。対象関数は形で発見するため、
  後から追加された対応表もテスト改修なしで比較対象になる。`tools/gen_api_parity.py`に
  `--espble-source`を追加。
- (EN) Add `interop/profile_wire`: the shared codec headers checked as a round trip
  between the two libraries instead of against their own vectors — a FLOAT32 read
  and notified, a CGM Measurement whose E2E-CRC one copy appends and the other
  verifies, an SFLOAT written the other way, and an iBeacon decoded from the
  advertisement alone. Every value is asserted as the wire bytes *and* as the
  decode in milli-units, so a drifted endianness, SFLOAT exponent nibble or CRC
  polynomial fails here even when both sides' unit tests still pass. First interop
  scenario with the roles reversed: this library is the GATT server and the beacon.
  Verified on hardware.
- (JA) `interop/profile_wire`を追加。共有codec headerを各自のvectorではなく2ライブラリ間の
  round tripで検証する。FLOAT32をRead / Notificationで、CGM MeasurementのE2E-CRCを一方が
  付与し他方が検証、SFLOATを逆方向のWriteで、iBeaconはadvertisementだけからdecodeする。
  各値をwire byteとmilli単位のdecode結果の両方でassertするため、endianness・SFLOATの指数
  nibble・CRC多項式が食い違えば、双方のunit testが通っていてもここで落ちる。interopで初めて
  役割を反転し、本ライブラリがGATT Server兼beaconになる。実機で確認済み。
- (EN) Report the peripheral half of the connection lifecycle. A peer that
  connected to this device's GATT Server existed on the air and nowhere in the
  API: no `onConnected()`, no MTU event, no entry in `connection()` /
  `connectionCount()`, no connection parameters, no HCI disconnection reason, and
  its pairing was dropped because the security callback required an active
  *central* link. All of it is now delivered with `localRole = Peripheral`
  alongside the central events, and the connection ID a GATT Server event carries
  is the one `connection()` resolves. Links this device opens as a central are
  filtered out by the link role, so a sketch that is both client and server sees
  one connection per link. Covered by `tests/peer/peripheral_connection` against a
  raw Arduino-ESP32 BLE client.
- (JA) Peripheral側の接続lifecycleを配送するようにした。GATT Serverへ接続してきた相手は
  電波の上には存在するのにAPI上に存在せず、`onConnected()`もMTU eventも
  `connection()`／`connectionCount()`のentryも接続パラメータもHCI切断理由もなく、pairingは
  security callbackが**Central** linkを要求していたため破棄されていた。現在はすべて
  `localRole = Peripheral`でCentral側と同じ経路で配送し、GATT Server eventが持つ
  connection IDは`connection()`で引ける。Centralとして自分から開いたlinkはlink roleで
  除外するので、ClientとServerを兼ねるsketchでも1 linkにつき1接続しか見えない。
  raw Arduino-ESP32 BLE client相手の`tests/peer/peripheral_connection`で確認。
- (EN) Add `add*Listener()`: several observers on one event, alongside the primary
  `on*()` callback. `EspBleListenerId`, `EspBleInvalidListenerId` and
  `EspBleCallbackList` are verbatim copies of EspBle's, and the connection, GATT
  client and GATT Server events all take listeners, so a profile helper no longer
  has to take the slot the application wanted — the prerequisite for porting the
  BLE MIDI and HID helpers. The primary runs first, then the listeners in
  registration order; four listeners fit per event and the fifth is refused with
  `EspBleInvalidListenerId`; ids are unique per owner; dispatch copies the
  callbacks out before calling them, so one may add or remove listeners without
  being invoked in that same dispatch. 26 rows left `docs/API_PARITY.tsv`.
  Covered by `tests/peer/multi_listener`.
- (JA) `add*Listener()`を追加。1つのeventをprimaryの`on*()`と複数のlistenerで観測できる。
  `EspBleListenerId`・`EspBleInvalidListenerId`・`EspBleCallbackList`はEspBleからの
  verbatim copyで、connection系・GATT Client系・GATT Server系のeventすべてがlistenerを
  受け付ける。profile helperがapplicationの枠を奪う必要がなくなり、BLE MIDI / HID helper
  移植の前提が整った。配送はprimary→登録順、1 event 4件までで5件目は
  `EspBleInvalidListenerId`で拒否、listener idはowner単位で一意。dispatchはcallbackを
  コピーしてから呼ぶため、callback内での追加・削除が同じdispatchに影響しない。
  `docs/API_PARITY.tsv`から26行が消えた。`tests/peer/multi_listener`で確認。
- (EN) CI: the unit-test step of `compile-examples.yml` now runs `pytest unit/`
  instead of repeating the g++ command lines, which had gone stale after the unit
  suites moved into per-suite directories (it still referenced
  `tests/unit/codec_test.cpp`) and only covered three of the nine suites.
- (JA) CI: `compile-examples.yml`のunit test stepを`pytest unit/`実行に変更。g++の
  コマンド列を二重管理していたため、unit suiteをsuite別ディレクトリへ移した後に古いパス
  （`tests/unit/codec_test.cpp`）を指したまま壊れており、9 suiteのうち3つしか実行できて
  いなかった。
- (EN) Add `tests/unit/api_parity`, `docs/API_PARITY.tsv`, and
  `tools/gen_api_parity.py`: every public-API difference from EspBle is now
  machine-checked and classified as `backend`, `classic`, or `planned`.
- (JA) `tests/unit/api_parity`・`docs/API_PARITY.tsv`・`tools/gen_api_parity.py`を追加。
  EspBleとの公開API差分を機械チェックし、`backend` / `classic` / `planned`に分類する。
- (EN) Add `EspBleBluedroid::NotificationCallback`,
  `ClientCharacteristicConfigurationUuid`, and
  `DisconnectReasonRemoteUserTerminated`, closing three EspBle naming gaps found
  by the parity check.
- (JA) parityチェックで見つかった3つの命名差を解消し、
  `EspBleBluedroid::NotificationCallback`・`ClientCharacteristicConfigurationUuid`・
  `DisconnectReasonRemoteUserTerminated`を追加。
- (EN) Port the shared `EspBleHidReportMap.h`, `EspBleKeymap.h` (with the layout
  tables) and `EspBleMidi.h` codecs verbatim from EspBle with their host unit
  tests, as the foundation for HID over GATT and BLE MIDI.
- (JA) 共通codec `EspBleHidReportMap.h`・`EspBleKeymap.h`（layout表を含む）・
  `EspBleMidi.h`をEspBleからverbatim移植し、host unit testも移植。HID over GATTと
  BLE MIDIの土台。
- (EN) Reject a malformed UUID string when a GATT Service, Characteristic, or
  Descriptor is registered. It used to be stored and then crash inside `begin()`,
  where the Arduino wrapper copies from an unset `BLEUUID`'s null native pointer.
- (JA) GATT Service / Characteristic / Descriptor登録時に不正なUUID文字列を拒否。
  以前はそのまま保持され、`begin()`中にArduino wrapperが未設定`BLEUUID`のnull nativeから
  コピーしてcrashしていた。
- (EN) Deliver a failure completion for a GATT operation that was in flight when
  the link dropped. A handle-addressed read or a service discovery waiting on a
  Bluedroid callback used to get no completion at all, leaving an application that
  awaits one callback per request waiting forever with no error. It now reports
  `InvalidState` with "connection closed before the GATT operation completed",
  covered by `tests/peer/gatt_disconnect_purge`.
- (JA) 切断時に実行中だったGATT操作へ失敗完了を配送。handle指定ReadやService Discoveryは
  Bluedroid callbackを待っていたため完了が一切届かず、1要求1 callbackで待つapplicationが
  エラーもないまま待ち続けていた。現在は`InvalidState`と
  "connection closed before the GATT operation completed"を配送する
  （`tests/peer/gatt_disconnect_purge`で確認）。
- (EN) Reword the duplicate-characteristic-UUID error: the restriction belongs to
  this library's UUID-addressed server API, not to Bluedroid, which can create
  such attributes.
- (JA) 重複Characteristic UUIDのエラー文言を修正。この制限はUUIDで属性を指す本ライブラリの
  Server APIによるもので、Bluedroid自体の制約ではない。
- (EN) Add peer coverage for a real GATT Indication, duplicate UUIDs
  (`duplicate_uuid`), reading above the MTU (`long_value`), and stack-owned
  Service Changed (`service_changed`), and an in-flight operation when the link
  drops (`gatt_disconnect_purge`). `long_value` disproved the documented
  assumption that a long read is truncated: Bluedroid continues the read and the
  whole value arrives, so the examples now say so.
- (JA) GATT Indicationの実発行、重複UUID（`duplicate_uuid`）、MTU超のRead
  （`long_value`）、stackが所有するService Changed（`service_changed`）、実行中GATT操作中の
  切断（`gatt_disconnect_purge`）のpeerテストを追加。
  `long_value`により「長いReadは切り詰められる」という既存記述が誤りであることが判明し、
  Bluedroidが内部で読みを継続して全体が返ることをexamplesへ反映した。
- (EN) Move the `security_bond` and `security_passkey` test UUIDs off the values
  EspBle uses, so the two libraries' suites can run near each other.
- (JA) `security_bond`と`security_passkey`のテスト用UUIDがEspBleと同一だったため変更。
  両ライブラリのテストを近接して実行できるようにした。
- (EN) Port the EspBle example set: 91 examples with bilingual READMEs, a shared
  `examples/DIFFERENCES_FROM_ESPBLE.md` list, and per-example "Differences from
  EspBle" sections where usage actually differs.
- (JA) EspBleのexample群を移植。91例をbilingual READMEで整備し、共通の
  `examples/DIFFERENCES_FROM_ESPBLE.ja.md`と、使い方が実際に異なるexampleごとの
  「EspBleとの違い」節を追加。
- (EN) Add the shared `EspBleMedicalFloat.h` and `EspBleCgmCrc.h` codecs with host
  unit tests, so standard Health and CGM examples encode the same wire bytes as EspBle.
- (JA) 共通codec `EspBleMedicalFloat.h`・`EspBleCgmCrc.h`をhost unit test付きで追加。
  標準HealthとCGMのexampleがEspBleと同じwire byteを生成する。
- (EN) Initial release
- (JA) 初期リリース
- (EN) Add runtime BLE passkey entry with two-board peer coverage.
- (JA) 実行時BLE passkey入力と2台peerテストを追加。
- (EN) Add LE Secure Connections Numeric Comparison with explicit confirmation.
- (JA) 明示確認を伴うLE Secure Connections Numeric Comparisonを追加。
- (EN) Cancel pending Security input on disconnect/end and cover rejection retry.
- (JA) 切断・終了時のSecurity入力待ち解除と拒否後の再試行を追加。
- (EN) Add deterministic Scan queue capacity, overflow, and flush coverage.
- (JA) Scan queue容量・overflow・flushの決定的テストを追加。
- (EN) Verify exact connection timeout and established-link shutdown semantics.
- (JA) 接続timeout分類と接続成立後の終了semanticsを実機確認。
- (EN) Add deterministic unanswered Passkey and Numeric Comparison timeout coverage.
- (JA) Passkey・Numeric Comparison未回答timeoutの決定的テストを追加。
- (EN) Add the Classic capability snapshot and asynchronous Inquiry facade
  with two-board peer coverage.
- (JA) Classic capability snapshotと非同期Inquiry facadeを2台peerテスト付きで追加。
- (EN) Add binary-safe Classic SPP Server sessions and reconnect coverage.
- (JA) binary-safeなClassic SPP Server sessionと再接続テストを追加。
- (EN) Add asynchronous Classic SPP Client connections using shared sessions.
- (JA) 共通sessionを使う非同期Classic SPP Client接続を追加。
- (EN) Verify active BLE Scan and binary SPP traffic on one dual-mode stack.
- (JA) 1つのdual-mode stack上のactive BLE Scanとbinary SPP trafficを検証。
- (EN) Verify GATT discovery, Characteristic traffic, notifications, and
  sustained SPP round trips on one dual-mode stack.
- (JA) 1つのdual-mode stack上でGATT Discovery・Characteristic通信・Notificationと
  継続的なSPP往復を検証。
- (EN) Add consecutive 64/128/256-notification dual-mode saturation rounds
  with per-round BLE event-drop accounting and lossless SPP receive-ring checks
  for delivered data.
- (JA) 同一接続上の64/128/256通知による段階的なdual-mode飽和試験を追加し、
  round別BLE event drop集計と配送済みdataのSPP受信ring保持を検証。
- (EN) Preserve BLE connection-control and GATT-completion events by evicting
  the oldest notification when the shared bounded queue is full.
- (JA) BLE connection event queue満杯時は最古のNotificationを退避し、接続制御・
  Security・GATT完了eventを保持。
- (EN) Add an ordered eight-entry SPP write queue with overflow diagnostics.
- (JA) 順序保証付き8件SPP送信queueとoverflow診断を追加。
- (EN) Add deferred SPP write-completion results with session, length, success,
  and error details for the shared Client/Server session API.
- (JA) Client/Server共通session APIへsession・byte数・成否・error detail付きの
  SPP送信完了eventを追加。
- (EN) Add a bounded 2048-byte SPP receive ring with Stream-like reads.
- (JA) Stream風readとoverflow診断を備えた2048 byte固定長SPP受信ringを追加。
- (EN) Add root-bound `EspBluedroidSppSerial`, an Arduino Stream/Print wrapper
  that automatically follows active SPP Server and Client sessions.
- (JA) active SPP Server/Client sessionへ自動追従するroot bindの
  Arduino Stream/Print実装`EspBluedroidSppSerial`を追加。
- (EN) Add Classic SSP Numeric Comparison and authenticated/encrypted SPP.
- (JA) Classic SSP Numeric Comparisonと認証・暗号化SPPを追加。
- (EN) Add separate Classic bond management, bonded reconnection, and secure
  SPP Client peer coverage.
- (JA) BLEとは分離したClassic bond管理、bond再接続、secure SPP Client実機テストを追加。
- (EN) Add address-scoped Classic DisplayOnly/KeyboardOnly Passkey Entry with
  two-way peer, unanswered-timeout, late-input rejection, bounded shutdown,
  and retry coverage.
- (JA) peer address付きClassic DisplayOnly/KeyboardOnly Passkey Entryと
  双方向・未回答timeout・遅延入力拒否・入力待ち終了・retry実機テストを追加。
- (EN) Move the primary build and hardware-test baseline to Arduino-ESP32 3.3.11.
- (JA) 主build・実機テスト基準をArduino-ESP32 3.3.11へ更新。
- (EN) Define the future multi-session SPP boundary: per-session resources and
  fairness, sticky automatic Serial selection, and a separate explicit-session
  Stream adapter.
- (JA) 将来のSPP複数session拡張について、session別resourceとfairness、stickyな
  自動Serial選択、明示session用Stream adapterの分離方針を定義。
- (EN) Add a raw Bluedroid feasibility test for two simultaneous SPP sessions
  over one ACL using distinct RFCOMM server channels.
- (JA) 異なるRFCOMM server channelを使い、同一ACL上で2本のSPP sessionを同時利用する
  raw Bluedroid成立性テストを追加。
