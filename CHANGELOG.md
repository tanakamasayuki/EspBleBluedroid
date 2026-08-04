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
  Verified on hardware. Note: EspBle 1.1.0's `addCharacteristic()` comment still
  says duplicates are rejected while its implementation registers them; the
  discrepancy is recorded in `tests/interop/README.md` rather than patched.
- (JA) `interop/duplicate_uuid`を追加。EspBle（NimBLE）Peripheralが同一Service内に置いた
  同一UUID Characteristic 2件——本ライブラリのServer APIが拒否する形であり、Client側は
  扱えなければならない形——に対して、Discoveryが2件を区別し、UUID指定は1件目に届き、
  Read、Write、購読、Notificationがすべて両側でhandleに帰属することを確認する。実機で確認済み。
  なおEspBle 1.1.0は`addCharacteristic()`のコメントで重複を拒否すると書いているが実装は
  登録する。この差異はpatchを当てず`tests/interop/README.md`に記録した。
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
