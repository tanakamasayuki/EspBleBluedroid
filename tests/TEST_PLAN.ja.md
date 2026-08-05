# テスト計画

> English: [TEST_PLAN.md](TEST_PLAN.md)
> 実行手順: [README.ja.md](README.ja.md)
> 設計上の位置づけ: [docs/API_DESIGN_POLICY.ja.md](../docs/API_DESIGN_POLICY.ja.md)、
> 現在地: [docs/STATUS.ja.md](../docs/STATUS.ja.md)

この文書は兄弟ライブラリ[EspBle](https://github.com/tanakamasayuki/EspBle)の
`tests/TEST_PLAN.md`と同じ構造を土台にし、Bluedroid固有の層（Bluetooth Classic、
dual mode、backend制約）を足したものである。EspBleと同名のscenarioは同じ意味で使い、
差分がある場所だけを明示する。

## 方針

BLEもClassicも、接続・切断・Discovery・購読・Security・Bondingが複数の非同期eventに
またがる。このためPeerテストを補助的なsmokeではなく、実装を進めるための主要な自動テストに
する。

- **unit**: backend非依存のcodec、parser、状態変換をhost上のg++で検証する（`tests/unit/`）。
  実機不要。API整合の機械チェック（後述）もここに置く。
- **examples_compile**: 公開APIと対象SoCのbuild回帰を検出する。`arduino-cli compile
  --profile esp32`で全exampleをコンパイルする（カバレッジ表のbuild列✅はこの検証を指す）。
- **peer**: 無印ESP32 2台を標準fixtureとし、実際のradio、controller、host stackを通した
  接続を検証する（`tests/peer/`）。
- **interop**: 無印ESP32（このライブラリ）＋ESP32-S3（**公開済みEspBleリリースパッケージ**）で、
  Bluedroid ↔ NimBLEのcross-stack相互接続を検証する（`tests/interop/`、実装後に追加）。
- **manual**: スマートフォン、PC、市販機器との相互運用を検証する。自動テストの合格条件へは
  混ぜず、[リリースチェックリスト](../docs/RELEASE_CHECKLIST.ja.md)で記録する。

Peer不要のruntime behaviorを1台で検証する「single」層は使用しない。必要になった時点で追加する。

## Peerハードウェア

| fixture | 親側DUT | 2台目Peer | 目的 | 接続方針 |
|---|---|---|---|---|
| 標準回帰 | 無印ESP32 | 無印ESP32 | 公開APIの全機能、Bluedroid経路、Classic、dual mode | 常時接続 |
| EspBle相互接続 | ESP32-S3（EspBleリリース） | 無印ESP32 | Bluedroid ↔ NimBLEのwire・手続き整合 | 常時接続（EspBleのメイン機材） |
| 手動相互運用 | 無印ESP32 | スマートフォン / PC / 市販機器 | OS実装との相互運用 | 手動 |

EspBleは無印ESP32では動作しない（NimBLEを直接使用し、Coreの制約でclassic ESP32を対象外に
している）。したがって相互接続fixtureの2台目は必ずS3系などNimBLE対応SoCになる。BLEもClassicも
ボード間配線は不要で、各ボードをPCへ接続するSerial/給電だけを使う。

pytest-embedded-cliの既存規約に従う。

- 親側profile: `esp32_peer_host`／`s3_peer_host`（interopのEspBle側）
- 2台目profile: `esp32_peer_device`
- 2台目directory: `peer_device/`
- Python fixture: `peers["device"]`

interopではS3を親fixtureにし、2台目は`peer/`と同じ無印ESP32を使う。したがって追加で必要な
設定は`TEST_SERIAL_PORT_S3_PEER_HOST`だけである。S3はEspBle側の常設機材なので、無指定の
`pytest`にもinterop suiteが含まれる。

`host` / `device`はpytest fixture上の識別名であり、BLE roleでもClassic roleでもない。
現行scenarioは親側をCentral / SPP Client / A2DP片側などに固定し、役割の入れ替えを前提にしない。
公開APIをCentralとして検証するときは親側の出力を主にassertし、Peripheralとして検証するときは
Peer側の出力を主にassertする。

## EspBleとのAPI整合をテストで固定する

「backend制約以外はEspBleに合わせる」という設計方針
（[docs/API_DESIGN_POLICY.ja.md](../docs/API_DESIGN_POLICY.ja.md)）は、文書だけでは
劣化する。次の5つをテストで固定する。

1. **名前と形の整合（unit）**: `api_parity`は`EspBle.h`と`EspBleBluedroid.h`の公開シンボル
   （class、method、struct field、enum定数）を突き合わせ、差分を`docs/API_PARITY.tsv`の
   許容表と照合する。表に理由付きで載っていない差分が出たら失敗する。EspBle側にしか無い
   API、こちら側にしか無いAPI、同名で引数が違うAPIをすべて分類させる。
   - 許容理由は`backend`（Bluedroid制約）、`classic`（Classic拡張でEspBleに存在しない）、
     `planned`（未実装、Issue/計画へのリンク必須）のいずれか。`planned`が残っている項目は
     「EspBle互換」と呼ばない。
2. **wire期待値の共有（peer / interop）**: EspBleと同名のscenarioは、同じ16進バイト列を
   期待値に使う。値が違う場合はbackend差ではなく実装バグとして扱う。
3. **差分は必ず明示エラーで観測する（peer）**: 制約で機能しない要求は、黙って成功したり
   無反応になったりせず`lastError()`と理由文字列を返す。テストはその文字列を固定する
   （例: 重複Characteristic UUID、legacy payload超過、実行中GATT操作の二重発行）。

4. **名前と形だけでなく戻り値も固定する（unit）**: signatureが全一致していても、関数が
   **何を返すか**はheaderに現れない。そこで`api_parity`は`*Name()`関数のenum→文字列対応も
   `espble.values` snapshotと比較し、差分は同じ表に載せる。これは実害から入った検証で、
   `lastErrorName()`がEspBleでは`INVALID_ARGUMENT`、こちらでは`InvalidArgument`を返して
   いたため、この文字列をログや比較に使うsketchは移植できなかった。現在はEspBleの綴りに
   合わせており、残る差分はこちらにしか存在するenum定数の`UNSUPPORTED`だけである。対象関数は
   形で発見するので、後から追加された対応表もテストを触らずに比較される。

5. **移植したファイルが移植したままであることを固定する（unit）**: `src/EspBleMidiProfile.h`
   は公開APIの比較対象ではなく、EspBleのファイルのライブラリ参照の型だけを差し替えたもので
   ある。`api_parity`は両側をコード行へ正規化し、`EspBleBluedroid`を`EspBle`へ書き換えた上で
   `espble.midi_profile` snapshotと完全一致することを要求する。片方のライブラリだけで挙動を
   変えると、signatureが全一致していてもここで落ちる。verbatim copy（`EspBleMidi.h`、
   `EspBleKeymap.h`など）はsnapshot不要で、素のdiffで足りる。

Classic拡張APIも同じ扱いにする。`classic().spp()`、`classic().a2dpSink()`などのsession API
は、EspBleのconnection APIと同じ語彙（非同期要求 → `update()`からの完了event、runtime ID、
`lastError()`、bounded queueとdrop計数）で検証する。**Classic側だけ別の作法になっていないこと
そのものをテストの観点にする。**

## テスト用UUIDの割り当て

周囲で別のテストが同時に走る前提で設計する。EspBleのpeer testが近くで動いていても
互いのpeerへ接続しないよう、**このrepository専用のUUID空間**を使う。

```text
SSSSNNNN-b1dd-4d00-9e5a-627564726f69
^^^^     suite tag（16-bit、下記の表）
    ^^^^ そのsuite内の属性番号（0000 = Service、0001以降 = Characteristic / Descriptor）
```

末尾の`627564726f69`はASCIIの`budroi`で、EspBle側のどのUUIDとも一致しない。新しいsuiteは
この表へ追記してから使う。

| suite tag | suite |
|---|---|
| `0001` | `gatt_disconnect_purge` |
| `0002` | `service_changed`のmarker service |
| `0003` | `duplicate_uuid` |
| `0004` | `long_value` |
| `0005` | `security_bond` |
| `0006` | `security_passkey` |
| `0007` | `peripheral_connection` |
| `0008` | `multi_listener` |
| `000b` | `duplicate_uuid_server` |
| `000c`〜`0011` | `hid_keyboard_device` / `hid_composite` / `hid_vendor_custom` / `hid_boot_protocol` / `hid_keyboard_host` / `hid_security` — tagのみ。HID over GATTのUUIDは仕様で固定されているため、デバイス名（`Bluedroid HID 000c`〜`0011`）で隔離する |
| `0009` / `000a` | `midi_device` / `midi_host` — **tagのみでUUIDではない**。BLE MIDIのServiceとCharacteristic UUIDは仕様で固定されているため、このsuiteは未使用UUIDを選べない。代わりにデバイス名で隔離する（`Bluedroid MIDI 0009`、`Bluedroid MIDI Peer 000a`）。両側とも名前とService UUIDの両方が一致することを要求する |
| `01xx` | interop scenario専用の範囲（`0100` = `interop/gatt_basic`、`0101` = `interop/advertise_scan`、`0102` = `interop/long_value`、`0103` = `interop/duplicate_uuid`、`0104` = `interop/security`、`0105` = `interop/profile_wire`、`0106` = `interop/midi`はtagのみ — BLE MIDIのUUIDは仕様固定なので、このscenarioは役割を含むデバイス名（`EspBle MIDI Device 0106` / `Bluedroid MIDI Device 0106`）で隔離する） |

既存suiteのうち上表に無いものは、移行前に個別に選んだ128-bit UUIDを使っている
（`8d47a6xx`、`6b976bxx`、`48e8c1xx`など）。これらもEspBle側と重複しないことを確認済みで、
触る機会があれば上の体系へ寄せる。`security_bond`と`security_passkey`はEspBleと**同一の
UUIDを使っていた**ため、混信源として先に移行した。

重複がないことは次で確認できる。

```sh
comm -12 \
  <(grep -rhoiE "[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}" tests/peer --include=*.ino | tr A-F a-f | sort -u) \
  <(grep -rhoiE "[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}" ../EspBle/tests/peer --include=*.ino | tr A-F a-f | sort -u)
```

標準profileのUUID（0x180a、0x2a05など）は仕様で決まっているため共有される。profile suiteでは
**marker用の独自Service UUIDをadvertiseして相手を選び**、標準UUIDだけで接続先を決めない。

## Peerテスト原則

- テスト専用128-bit Service UUID / 専用RFCOMM名で周囲の機器を除外する。上の割り当て表から
  未使用のsuite tagを取る。
- device nameだけで接続相手を決めない。
- 可能な範囲で一方をArduino-ESP32同梱API、またはraw ESP-IDF/Bluedroid APIの直接実装にする。
  公開APIどうしだけの通信では、library固有の思い込みが両端で相殺されて見えなくなる。
- Serial logだけで合否をassertできるscenarioにする。
- 各テスト終了時にscan、advertising、subscription、connection、SPP session、audio streamを
  停止する。
- Securityテストは開始時と終了時のBond / NVS状態を明示する。BLE bondとClassic bondは
  別storeなので、どちらを見たのかをテスト名と出力に出す。
- radio環境による一時的な遅延にtimeoutは許すが、無制限retryで不具合を隠さない。
- 接続・切断理由、MTU、Security状態を可能な限り両側で照合する。
- **`update()`配送を明示的に確認する**。callbackがstack task上ではなく利用者の`loop()`
  contextで呼ばれることを、出力に`context=loop`を含めて固定する。A2DP/HFPのPCM callbackだけは
  stack task上で呼ばれる例外なので、そのcontextも同様に出力へ出す。
- **bounded queueは溢れさせて数える**。scan result 16件、BLE connection event、SPP write 8件、
  SPP RX ring 2048 byteは、上限そのものを仕様として固定するのではなく、超過時のdrop計数
  （`droppedResultCount()` / `droppedEventCount()`など）が正しいことを固定する。
- **test-only seam**（`ESP_BLE_BLUEDROID_TESTING`）は、外から決定的に再現できない経路
  （queue overflow、security timeout短縮）だけに使う。公開APIで再現できる経路には使わない。

## EspBleリリースパッケージとの相互接続suite（`interop/`）

同梱Bluedroidどうしの通信だけでは、実装が「Bluedroidの癖」に依存していても気づけない。
EspBle（NimBLE）を相手にしたcross-stack試験をこのrepositoryへ置く。

### 依存の固定

- 開発中の`../EspBle`、default branch、未release commitは**基準にしない**。
- Arduino library index上の公開releaseを`sketch.yaml`で固定する
  （`libraries: [- EspBle (1.1.0)]`）。platform versionと同じ書き方で、Arduino CLIがその
  releaseをinstallする。手動の取得・展開は不要。実行手順は
  [interop/README.ja.md](interop/README.ja.md)。
- installされたpackageへpatchを当てない。EspBle側を直さないと通らない場合は、その事実を
  該当箇所つきで[../docs/ESPBLE_FEEDBACK.ja.md](../docs/ESPBLE_FEEDBACK.ja.md)へ残す。
  片方の実装しか埋めていないfieldも同じ扱いで、共通の期待値からは外す。
- version更新は自動追従させず、別途のreleaseツールによる明示的な変更として扱い、差分と
  全相互接続結果をreviewする。

### 実行

3台とも常設なので、無指定の`pytest`にすべての層が含まれる。individual実行は次のとおり。

```sh
uv run --env-file .env pytest interop/
```

`.env`には無印ESP32 2台に加えて`TEST_SERIAL_PORT_S3_PEER_HOST`を置く。テスト側にfixture用の
conftest hookは持たず、port設定と`sketch.yaml`だけで構成する。

### 対象scenario（実装が固まった順に追加）

| scenario | 内容 |
|---|---|
| `interop/gatt_basic` | ✅ Bluedroid Central ↔ EspBle Peripheral。MTU 247交換、宣言propertyを含むDiscovery、Read、応答あり/なしWrite、Descriptor Read/Write、Notify、確認応答を伴うIndicate、購読解除、切断。逆向き（EspBle Central ↔ Bluedroid Peripheral）はPeripheral connection snapshot実装で可能になった（`interop/profile_wire`が既に本ライブラリをServerとして動かしている）。専用scenarioとして追加予定 |
| `interop/advertise_scan` | ✅ EspBleのpayload builderが出したAdvertising / Scan Responseを、Bluedroid Scannerがaddress単位でmergeして同じfieldへ復元すること（およびその逆）。同じadvertiserをpassive scanしたときはAdvertising payloadのfieldだけが見え、Scan Response側は一切見えないこと |
| `interop/security` | ✅ Just Works、静的passkeyのPasskey Entry、Numeric Comparison（承認と拒否）をcross-stackで検証。encrypted / authenticated / bonded / key sizeを**両側**でassertし、bondも両側で確認し、attribute権限の2段（authenticated CharacteristicがJust Works linkでは拒否され、authenticated linkでは到達できること）も確認する。Numeric Comparisonでは2実装が**同一の6桁**を導出したこと、拒否時はどちらにも暗号化もbondも残らないことをassertする。Bluedroid Peripheral側はconnection snapshot実装で可能になり（`peer/peripheral_connection`がその役割でのpairingを報告する）、追加予定 |
| `interop/profile_wire` | ✅ 共有header（`EspBleMedicalFloat.h`、`EspBleCgmCrc.h`、`EspBleIBeacon.h`）で組んだ値が相手stackで同じ値としてdecodeできること。wire byteとdecode結果（milli単位の整数）の両方でassertする。FLOAT32をReadとNotificationで、CGMのE2E-CRCを一方が付与し他方が検証、SFLOATを逆方向で、iBeaconはadvertisementだけからdecode。interopで初めて役割を反転させ、被検ライブラリがServer兼beaconになる |
| `interop/duplicate_uuid` | ✅ 仕様が認める重複UUID（EspBle Peripheralが同一Service内に同一UUID Characteristicを2つ）を、Bluedroid Clientがhandle指定で扱えること。Discoveryで2件を区別、UUID指定は1件目に届く、Read / Write / 購読 / Notificationがすべて両側でhandleに帰属することを確認。本ライブラリ側で同じ形を登録できることも同じファイルに記録する（公開されたdatabaseの読み出しは`peer/duplicate_uuid_server`） |
| `interop/long_value` | ✅ EspBle Peripheralが公開した合意MTU超の値が、UUID指定・handle指定の両方のReadで全体として返ること。`peer/long_value`は両端がBluedroidなので、clientの性質として言えるのはこちら |
| `interop/midi` | ✅ BLE MIDIを両方向で検証し、各側が自分のライブラリでencode / decodeする。data byteが2・1・0個のchannel voiceメッセージと、rampを保ったまま再構成される99 byteのSysEx。codec headerはbyte一致、profile helperは型1つだけの差で、どちらも機械チェック済みなので、ここで足すのはtransport側（CCCD write、negotiated MTUに対するNotification、Write Without Response、packetを跨ぐSysEx）である。2つ目のテストでは役割を入れ替える — PeripheralとしてnotifyするのとCentralとしてwriteするのは別経路だからである。BLE MIDIのUUIDは仕様固定なので、隔離はデバイス名で行う |
| `interop/hid` | ✅ HID over GATTを双方向で、各側が自分のライブラリでencodeとdecodeを行う。Report Descriptorのバイト列一致と共有parserは既に機械的に検査済みなので、ここで追加されるのは「**別実装**が電波上で同じ結論に達するか」である: すべてがUUID 0x2A4Dを共有するcharacteristicの中でReport Referenceが同じreportを指すこと、キー入力が同じusageと（各スタック自身のlayoutを通って）同じ文字にdecodeされること、modifierが単独のusageとしても報告されること、LED writeが逆方向へ届くこと。2つ目のtestで役を入れ替え、片側だけが常にdecodeする側にならないようにしている。隔離はデバイス名: HIDのUUIDは仕様で固定されている |

自動で合否を決められるscenarioだけを対象にする。スマートフォン操作、GUI確認、聴感評価、
手動pairing操作は含めず、リリースチェックリストの手動相互運用へ分離する。

## カバレッジ計画

`build`列はexampleコンパイル、`peer`列は無印ESP32 2台、`interop`列はEspBle相手の
cross-stack試験を指す。

### BLE共通面（EspBleと同じ観点）

| 領域 | unit | build | peer | interop |
|---|---|---|---|---|
| test fixture / backend成立性 | | ✅ | ✅ `stack_smoke` | |
| Advertising / Scan parser | 予定 | ✅ | ✅ `advertise_scan` / `advertise_payload` | ✅ `advertise_scan` |
| Scan Response分割 / Appearance / Tx Power | | ✅ | ✅ `advertise_scan`に同梱 | ✅ `advertise_scan`に同梱（activeのmergeとpassiveの対比） |
| Advertising Service Data（AD 0x16） | | ✅ | ✅ `advertise_scan`に同梱 | ✅ `advertise_scan`に同梱 |
| non-connectable broadcast | | ✅ | ✅ `ibeacon` | |
| iBeacon encode / decode | ✅ `unit/ibeacon` | ✅ | ✅ `ibeacon` | ✅ `profile_wire` |
| UUID codec | ✅ `unit/uuid` | ✅ | — | |
| connect / disconnect / timeout / 切断理由 | | ✅ | ✅ `connect_disconnect` | ✅ `gatt_basic` |
| MTU交換（23→合意値、遅延要求） | | ✅ | ✅ `connect_disconnect`に同梱 | ✅ `gatt_basic` |
| 接続パラメータ | | ✅ | ✅ `connection_parameters` | |
| own address / Tx Power | | ✅ | ✅ `local_identity` | |
| Filter Accept List（advertising / scan） | | ✅ | ✅ `accept_list` | |
| Directed Advertising | | ✅ | ✅ `directed_advertising` | |
| GATT Client Discovery / Read / Write / Descriptor / Notify | ✅ `unit/codec` | ✅ | ✅ `gatt_client` | ✅ `gatt_basic` |
| GATT Client handle指定操作（重複UUID） | | ✅ | ✅ `duplicate_uuid` | 予定 `duplicate_uuid` |
| GATT Client 1操作ずつの直列化と明示拒否 | | ✅ | ✅ `gatt_client`に同梱 | |
| MTU超の値のRead（全体が返ること） | | ✅ | ✅ `long_value` | ✅ `long_value` |
| GATT Server Read / Write / Descriptor / CCCD / Notify | | ✅ | ✅ `gatt_server` | ✅ `gatt_basic` |
| GATT Server **Indicate**（実発行と確認応答） | | ✅ | ✅ `gatt_server` / `service_changed` | ✅ `gatt_basic` |
| GATT Server 重複UUIDの公開とhandle指定 | | ✅ | ✅ `duplicate_uuid` ＋ `duplicate_uuid_server` | |
| Service Changed（0x2A05、stackが所有） | | ✅ | ✅ `service_changed` | |
| 実行中GATT操作の切断時の扱い | | ✅ | ✅ `gatt_disconnect_purge` | |
| Pairing / Bonding（Central） | | ✅ | ✅ `security_bond` | ✅ `security` |
| 静的passkey / MITM / authenticated attribute | | ✅ | ✅ `security_passkey` | ✅ `security` |
| 実行時Passkey Entry | | ✅ | ✅ `runtime_passkey` | 予定 `security` |
| Numeric Comparison（確認 / 拒否 / timeout） | | ✅ | ✅ `numeric_comparison` | ✅ `security`内（確認 / 拒否） |
| Peripheral connection snapshot / security event | | ✅ | ✅ `peripheral_connection` | |
| lifecycle反復 / heap / task / event leak | | ✅ | **未** → `lifecycle_stress` | |
| Wi-Fi / BLE共存（無印ESP32の内蔵radio共有） | | ✅ | **未** → `wifi_ble_coexistence` | |
| PHY更新 | — | — | **対象外**（Bluetooth 4.2 LE、2M/Coded PHYなし） | |
| persistent subscription / auto-reconnect | — | — | **対象外**（API非提供、EspBleとの差分として文書化済み） | |
| 複数同時接続 | — | — | **対象外**（Central 1接続、Peripheral 1接続） | |

### 標準GATT profile（examplesと1対1）

現状**peer列は全件未実装**であり、[examples](../examples/)のprofile sketchはコンパイル確認
のみである。wire形式が誰にも検証されていないことを明示するため、行を落とさず「未」で残す。

| profile | unit | build | peer | interop |
|---|---|---|---|---|
| Battery Service | | ✅ | 未 `battery_service` | |
| Device Information Service | | ✅ | 未 `device_information` | |
| Current Time / Reference Time Update | | ✅ | 未 `current_time` / `reference_time_update` | |
| Heart Rate | | ✅ | 未 `heart_rate` | 予定 `profile_wire` |
| Health Thermometer | ✅ `unit/medical_float` | ✅ | 未 `health_thermometer` | 予定 `profile_wire` |
| Blood Pressure | ✅ `unit/medical_float` | ✅ | 未 `blood_pressure` | |
| Pulse Oximeter | ✅ `unit/medical_float` | ✅ | 未 `pulse_oximeter` | |
| Weight Scale / Body Composition | | ✅ | 未 `weight_scale` / `body_composition` | |
| Glucose（RACP手続き） | ✅ `unit/medical_float` | ✅ | 未 `glucose` | |
| Continuous Glucose Monitoring | ✅ `unit/cgm_crc` | ✅ | 未 `continuous_glucose_monitoring` | 予定 `profile_wire` |
| Environmental Sensing | | ✅ | 未 `environmental_sensing` | |
| Cycling Speed and Cadence / Power | | ✅ | 未 `cycling_speed_cadence` / `cycling_power` | |
| Running Speed and Cadence | | ✅ | 未 `running_speed_cadence` | |
| Fitness Machine（FTMS） | | ✅ | 未 `fitness_machine` | |
| Location and Navigation | | ✅ | 未 `location_navigation` | |
| User Data | | ✅ | 未 `user_data` | |
| Alert Notification / Immediate Alert / Phone Alert Status | | ✅ | 未 `alert_notification` / `immediate_alert` / `phone_alert_status` | |
| Proximity（Link Loss + Tx Power） | | ✅ | 未 `proximity` | |
| Bond Management | | ✅ | 未 `bond_management` | |

### HID / MIDI（未実装、[Phase 1](../docs/PROFILE_BRIDGE_ROADMAP.ja.md)）

実装漏れであり、backend上の不可能要因ではない。前提条件を満たした順に有効化する。

| 領域 | unit | build | peer | interop |
|---|---|---|---|---|
| HID Report Map parser | ✅ `unit/report_map` | — | — | |
| 公開するHID Report Descriptor（wire仕様） | ✅ `unit/hid_report_maps`（EspBleとbyte一致、および意味の再解析） | — | — | |
| keyboard layout / keymap | ✅ `unit/keymap` | — | — | |
| BLE MIDI packet codec | ✅ `unit/midi` | — | — | |
| 複数observer配送（`add*Listener()`） | | ✅ | ✅ `multi_listener` | |
| BLE MIDI Device / Host | ✅ 上記codec | ✅ | ✅ `midi_device` / `midi_host` | ✅ `interop/midi`（両方向） |
| HID Device — keyboard | ✅ 上記descriptor | ✅ | ✅ `hid_keyboard_device` | ✅ `interop/hid`（EspBleのhost相手） |
| HID Device — mouse / consumer control / system control / gamepad | ✅ 上記descriptor | ✅ | ✅ `hid_composite` | `interop/hid`の対象外（同scenarioはkeyboard profileを両方の役で扱う） |
| HID Device — vendor / `hidCustom()` | ✅ 上記descriptor | ✅ | ✅ `hid_vendor_custom`（双方向、Output・Feature Report） | `interop/hid`の対象外 |
| HID Device — boot protocol | ✅ 上記descriptor | ✅ | ✅ `hid_boot_protocol`（NKRO ⇄ boot変換、rollover、mode別の`ready()`） | |
| HID Device — security tier | | ✅ | ✅ `hid_security`（deviceは接続時にPairingを要求する。**拒否した**HostはReport Map・HID Information・全Report Referenceから値を得られず、属性の存在自体は見えたまま。拒否は`onSecurityChanged(success=0)`として報告され、Just Worksのbondを受ければ同じ属性が読める） | |
| HID Device — robustness | | | 大半は他suiteでカバー済み: 2種類の送信拒否と切断時リセットは`hid_keyboard_device`、rolloverは`hid_boot_protocol`、長さ不一致と未宣言reportの拒否は`hid_vendor_custom`。残りはHID固有ではない（`lifecycle_stress`） | |
| HID Host | ✅ 上記parser | ✅ | ✅ `hid_keyboard_host`（デバイス自身の属性からのdiscovery、usage→文字、状態と差分、LED write） | ✅ `interop/hid`（EspBleのdevice相手） |
| HID Host — key slotのerror code | | | ✅ `0x01`（ErrorRollOver）は`hid_boot_protocol`。`hid_keyboard_host`が`0x02`（POSTFail）を入れたslotを送り、押下として報告されないことと`invalidInputReportCount()`が数えることを固定（[../docs/ESPBLE_FEEDBACK.ja.md](../docs/ESPBLE_FEEDBACK.ja.md)） | |

### Bluetooth Classic / dual mode（このライブラリ固有）

| 領域 | unit | build | peer |
|---|---|---|---|
| capability / profile対応理由 | | ✅ | ✅ `classic_inquiry`に同梱 |
| Inquiry（name / CoD / RSSI / 停止 / 完了） | | ✅ | ✅ `classic_inquiry` |
| SPP Server / Client | | ✅ | ✅ `spp_server` / `spp_client` |
| SPP RX ring（binary / overflow / 切断時無効化） | | ✅ | ✅ `spp_receive_buffer` |
| SPP Stream wrapper | | ✅ | ✅ `spp_serial` |
| SPP複数session（raw feasibility） | | — | ✅ `spp_multi_backend` |
| Classic Security（SSP / Passkey / bond store） | | ✅ | ✅ `spp_security` / `spp_passkey` |
| A2DP Sink / Source + AVRCP | | ✅ | ✅ `a2dp_sink` / `a2dp_source` |
| A2DP長時間soak（underrun / heap / latency） | | — | **未** → `a2dp_soak` |
| HFP HF / AG（SLC / SCO / CVSD / mSBC） | | ✅ | ✅ `hfp_backend` |
| BLE + SPP dual mode | | ✅ | ✅ `dual_mode_scan_spp` |
| profile間resource競合（A2DP + SPP同時など） | | — | **未** → `profile_resource_conflict` |
| Classic session APIのEspBle語彙整合 | ✅ `unit/api_parity` | ✅ | 既存suiteの観点として追加 |

## 実装済みscenario

現状: peer 31 suite / 37 test関数、unit 9 suite / 12 test関数（exampleのbuild確認は91件）。

1. ✅ `stack_smoke`: Arduino-ESP32同梱APIで2台接続、GATT read/write、CCCD購読、notification。
2. ✅ `advertise_scan`: 公開APIのlifecycle、Advertising / Scan Response二面構成、Service Data・
   Appearance・Tx Powerを含むactive Scan merge、payload超過拒否、値型result、duration停止と
   明示停止、16件queueへ18件を決定的に注入したdrop計数、`end()`時flush、再初期化。
3. ✅ `advertise_payload`: raw AD構造、複数UUIDの集約、31 byte境界、時間指定停止。
4. ✅ `ibeacon`: 共有codecでのiBeacon encode → broadcast → scan → decode。
5. ✅ `connect_disconnect`: non-blocking接続、再接続ID、23→合意値のMTU交換、HCI切断理由、
   非advertising peerへのtimeout分類、接続試行中と接続確立後の`end()`、peer切断、再初期化。
6. ✅ `connection_parameters`: 初期snapshot、Centralからの更新要求、両peerの合意値、`update()`配送。
7. ✅ `local_identity`: Random Static / RPA、現在アドレスと観測値の一致、−12/+9 dBmと電波上の
   Tx Power Level。
8. ✅ `accept_list`: 初期化前拒否、重複登録、一覧外Centralの接続拒否、Scan側`acceptListOnly`、
   `Any`変更後の接続と切断。
9. ✅ `directed_advertising`: 宛先CentralへのpayloadなしHigh Duty、接続・切断、1.28秒自動停止、
   Low Duty継続と明示停止。
10. ✅ `gatt_client`: connection単位のdatabase snapshot、Characteristic単体探索、UUID / handle
    指定Characteristic操作、Descriptor Read / Write、Notification購読・解除、binary-safe値、
    切断時の無効化、`update()`配送。
11. ✅ `gatt_server`: 静的GATT Server、動的Read、binary Write、Descriptor Write、CCCD購読、
    Notification、**Indication**（peerがCCCDへ0x0002を書き、確認応答が返ってから
    `onSent()`がsuccessを報告すること）、送信完了、`update()`配送。
12. ✅ `security_bond`: Just Works、暗号化GATT、bond保存、暗号化再接続、security callback、bond削除。
13. ✅ `security_passkey`: 静的passkey MITM、passkey表示、authenticated GATT、bond保存。
14. ✅ `runtime_passkey`: KeyboardOnlyの実行時passkey入力、入力待ち中の`disconnect()` / `end()`、
    未回答timeout、直後のretry、再初期化。
15. ✅ `numeric_comparison`: DisplayYesNoの6桁一致、明示拒否（linkは維持）、未回答timeout、retry。
16. ✅ `classic_inquiry`: dual-mode初期化、compile-time capability snapshot、Classic name /
    Class of Device / RSSI、result callback内からの停止、完了eventの`update()`配送。
17. ✅ `spp_server`: raw ESP-IDF Client相手のbinary-safe双方向data、再接続ID、remote切断、
    稼働中`end()`、8件送信queueの順序とoverflow、受理8件の送信完了。
18. ✅ `spp_client`: 非同期SDP / RFCOMM接続、共通session API、binary data、送信完了、local切断、
    再接続ID、失敗 / timeout配送。
19. ✅ `spp_receive_buffer`: 2048 byte固定長RX ring、binary read、overflow byte数、切断時無効化。
20. ✅ `spp_serial`: rootへbindした`EspBluedroidSppSerial`、連続2 sessionへの自動追従、
    `Stream` / `Print`、1000 byte分割write、`flush()`、切断後の無効化。
21. ✅ `spp_security`: Client / Server両roleのDisplayYesNo SSP、明示拒否、認証失敗後のretry、
    Classic bond列挙・再接続・削除、認証・暗号化data。
22. ✅ `spp_passkey`: Classic DisplayOnly / KeyboardOnlyの両方向、未回答timeout、期限後入力の拒否、
    retry、入力待ち中の`end()`、I/O capability反転での再初期化。
23. ✅ `spp_multi_backend`: raw Bluedroidで同一ACL上の2 SCNへ2 session同時接続、handle別双方向data、
    両session切断（公開API拡張の前提となるfeasibility）。
24. ✅ `a2dp_sink` / `a2dp_source`: 公開A2DPとraw相手のPCM、AVRCP Play/Pause Press・Release、
    absolute volume、callback context（PCMはstack task、controlは`update()`）、切断・終了。
25. ✅ `hfp_backend`: 公開HFP Hands-Free / Audio Gateway間のSLC、SCO、CVSD / mSBC mono PCM
    双方向data、切断。
26. ✅ `dual_mode_scan_spp`: active SPP session中のBLE Scan・GATT接続・Discovery・Read / Write、
    同一接続で64→128→256通知、round別BLE event drop集計、配送済み通知のSPP往復、
    満杯時のGATT完了優先配送。
27. ✅ `long_value`: MTUを超える値のRead。UUID指定・handle指定の両方で全体（300 byte）が返り、
    既知のrampと1 byteずつ一致すること。BluedroidにRead Blobの公開APIがないため切り詰めを
    想定していたが、実機で内部継続が確認されたため契約として固定した。
28. ✅ `duplicate_uuid` / `duplicate_uuid_server`: Server側は同一Service内の重複Characteristic
    UUIDを専用handleつきで受理し（同一Characteristic下の重複Descriptor UUIDと不正UUIDは
    従来どおりerror名・detail文字列まで固定して拒否）、別Serviceの同一UUIDも受理する。
    公開されたdatabaseに実体が2つあることは`duplicate_uuid_server`がraw peerから読み出して
    確認する（登録の受理だけでは、先頭entryを再利用するbackendでも通ってしまう）。
    Client側はpeerの同一UUID Characteristic 2件を別handleとしてsnapshotへ保持し、
    handle指定で個別Read・購読し、Notificationが送信元handleへ対応することを確認する。
29. ✅ `service_changed`: Generic Attribute 0x1801とService Changed 0x2a05はstackが公開する。
    applicationが登録しなくてもpeerからindicatableな0x2a05が見えることを確認し、
    `notifyServicesChanged()`相当がこのライブラリに無い理由を固定する。
30. ✅ `gatt_disconnect_purge`: 実行中Read中の`disconnect()`が受理され（拒否ではない）、
    その操作の完了が**1件だけ**届き、`droppedEventCount()`が0で、続く再接続でDiscoveryと
    Readが通ること。あわせてGATT登録時の不正UUID拒否も`duplicate_uuid`で固定した。
31. ✅ host unit test: `uuid`、`codec`、`ibeacon`、`medical_float`、`cgm_crc`、`report_map`、
    `keymap`、`midi`、`hid_report_maps`、`api_parity`。

## 優先順位

現在の空白のうち、実装作業を伴わないものから着手する。

**P1: 実装0行で埋まる穴（このうち残りは1件）**

- ✅ `unit/api_parity` と `docs/API_PARITY.tsv`（EspBleとの差分を機械チェックへ変換した）
- ✅ `unit/report_map` / `unit/keymap` / `unit/midi`（EspBleからheaderごと移植。HID / MIDIの土台）
- ✅ `gatt_server`への**Indicate実発行**追加
- ✅ `duplicate_uuid`（登録契約とエラー文字列の回帰。HID前提で重複Characteristic UUIDの
  制限を解除したため、Server側のassertionは反転済み。公開の実体確認は`duplicate_uuid_server`）
- ✅ `service_changed`（stackが所有することの固定）
- ✅ `long_value`（MTU超Readで全体が返ることの固定。当初の「切り詰め」想定は実機で否定された）
- ✅ `gatt_disconnect_purge`（実行中GATT操作中の`disconnect()`が受理され、完了が1件だけ届き、
  drop 0で、次の接続のDiscoveryとReadが通ること）
- ✅ 実装修正3件（テスト作成中に判明）: 切断時に実行中だったGATT操作へ失敗完了を配送する
  （以前は完了が届かず、applicationが永久に待つ経路だった）、 不正なUUID文字列をGATT登録時に拒否する
  （以前は`begin()`中にwrapperのnull nativeでcrashしていた）、および重複Characteristic UUIDの
  エラー文言をBluedroidの制約ではなくライブラリの制約として正しく述べる。

**P2: 既存実装の穴**

- 標準GATT profile peer test 24件（examplesと1対1、wire期待値はEspBleと共有）
- `lifecycle_stress`
- `wifi_ble_coexistence`（無印ESP32の内蔵radio共有）
- `a2dp_soak`、`profile_resource_conflict`

**P3: 基盤API追加を伴うもの**

- ✅ Peripheral connection snapshot / security event（`peripheral_connection`）。
  interop scenarioの逆方向、Security Server scenario、
  [examples/Security](../examples/Security/)の非対称解消、HID Deviceの前提を解禁した
- ✅ `add*Listener()`（`multi_listener`）。event毎にprimary＋listener 4件、idはowner単位、
  dispatch中の追加・削除の規則。MIDI / HID helperをそのまま移植できる状態になった

**P4: profile実装**

- ✅ BLE MIDI Device / Host（`src/EspBleMidiProfile.h`、`midi_device` / `midi_host`、
  [examples/Midi](../examples/Midi/)）。helperはEspBleのファイルのライブラリ参照の型だけを
  差し替えたもの。peer test側はBLE MIDIのヘッダを自前の演算でデコードするため、同じcodecを
  2回突き合わせるのではなく仕様と突き合わせている
- ✅ BLE HID Device、全profile（`src/EspBleBluedroidHid.cpp`、
  [examples/Hid](../examples/Hid/)）: keyboard（`hid_keyboard_device`）で
  `docs/API_PARITY.tsv`から81行が消え、mouse・consumer control・system control・
  gamepadを共有manager経由で実装（`hid_composite`）して差分は715行から547行へ。続いて
  `hidVendor()`と`hidCustom()`（`hid_vendor_custom`）——ライブラリが中身を解釈しない2つの
  profileで、Hostから書き込まれる唯一のprofileでもある——で547行から502行へ。さらに
  Boot Protocol（`hid_boot_protocol`）——NKRO keyboardがboot Hostへ固定8 byteのreportで
  答える経路
- ✅ BLE HID Host（`src/EspBleBluedroidHidHost.cpp`、`hid_keyboard_host`）。差分は502行から
  395行へ。DeviceはReport Mapを公開し全reportがUUID 0x2A4Dを共有するので、Host側は
  descriptorとReport Referenceを読み、想定レイアウトではなく読み取った内容でdecodeする
  （parserはDevice側と共有）。discoveryはEspBleの直線的な手順ではなく状態機械。
  本backendは1 linkあたりCentral GATT操作を同時1件しか許さないため。
  ✅ `interop/hid`が双方向で完了——EspBleのdeviceに対する本実装のhostと、その逆。
  descriptorのバイト列ではなく「その意味」について別実装と一致することを確認した最初の
  テストである。残りはsecurityのscenario

**interop**: 各層でAPIとwire動作が固まった順に`interop/`へ写す。`gatt_basic`、
`advertise_scan`、`long_value`、`duplicate_uuid`、`security`、`profile_wire`は実装済み。
残るのは接続系scenarioの逆方向（Peripheral connection snapshotが実装されたので着手可能）と
HID/MIDI。

## 既知の間欠失敗

full runで1度だけ観測され、狙った反復では再現しなかった失敗。次の調査が「何を既に潰したか」から
始められるように記録する。

**Resolvable Private AddressがIdentity addressにフォールバックした**（`local_identity`、
76テストのfull runで1度）。RPAフェーズで、peripheralは`type=1`（Random）と報告しながら、
observerはその工場出荷public addressを`type=0`で観測した:

```
peer: RPA_READY 1 error=NONE
peer: LOCAL address=- type=1
dut:  OBSERVED address=00:70:07:0e:9b:0e type=0 txpower=9
```

報告値と電波上の実体が食い違っているので、再発するならcontrollerまたはstack側のフォールバックで
あり、待ち漏れではない。4通りの条件で計54回のRPAモード観測を行い、すべて正常だった
（毎回異なるアドレスで上位2 bitが`01`）:

- `end()` → `begin(ResolvablePrivate)` → `advertising.start()` の遷移を15回反復。
  privacy完了待ちの競合ではない（ライブラリは
  `ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT`を待ち、statusも確認している）
- `peer/security_bond`でbond storeを作ってから同じ反復を15回。bond / IRK状態依存ではない
- 失敗したattemptが実際に経ていた順序——RPAモードでは`advertising.start()`のたびに
  privacy off → random address設定 → privacy on を再実行し、tx powerコマンドは
  advertisingを再起動するので、失敗時はその3回のトグルの後だった——を8周replayし、
  各トグル後にも観測して計24回

full runにあってこれらに無かったもの: 先行する約74テスト（同じdual mode controller上の
Bluetooth Classic suite——A2DP・HFP・SPP——を含む）と、混雑した2.4 GHz環境。再現には
遷移の反復回数よりもこの負荷が必要と思われる。

コードを読む過程で弱点が1つ見つかった。これが原因かどうかとは無関係に直す価値がある:
`EspBleAdvertising`のidentity適用経路の完了待ちが、要求ごとの状態ではなく共有の単一atomic
（`privacyOperationCompleted`、`randomAddressOperationCompleted`）を使っているため、
前の操作の遅れた完了イベントが次の待ちを満たしうる。ただしこれ単独では今回の失敗を説明できない
——stale eventが残るには前の待ちがタイムアウトしている必要があり、その場合`begin()`/`start()`が
失敗するが、ログには失敗前に`restarted=1`が3回成功して並んでいる。それでも
「任意の先行イベントが満たせる待ち」はそれ自体が誤りである。

**修正済み（この失敗の原因とは確定していない）**: 共有フラグを`BackendGapOperation`の
チケット方式へ置き換えた（`src/EspBleBluedroid.cpp`）。各コマンドがチケットを取り、完了イベントは
カウンタを進め、待ちは自分のチケットに到達したカウントだけを受け付ける。コマンドより前のイベントが
その待ちを満たすことはもう起こらない。タイムアウト時はカウントを再同期してから失敗する
——ずれを残すと以後の全操作が失われたイベントを待つことになり、1件の取りこぼしがセッション全体を
壊すため。同じ形をしていた他の待ちも同じ方式へ揃えた——accept listの書き込み、directed
advertisingの開始、scannerのparameter / start / stopである。`local_identity` /
`directed_advertising` / `accept_list` / `advertise_scan` / `long_value` /
`connect_disconnect`で実機確認済み。

なお、scanの置き換えは後述のscan missを説明しない。あのsketchはscanを1度しか開始しないので、
自分のものと取り違えうる先行操作がそもそも存在しない。

**scanがadvertisementを1件も受け取らなかった**（`long_value`、同じrun）。両ボードは正常起動して
おり（`LONG_VALUE_READY`、`LONG_VALUE_PEER_READY length=300`）、DUTの継続scanがpeerの
service UUIDに一致する結果を配送しなかったため`TARGET_FOUND`が印字されなかった。
advertisementの取りこぼしは無線として正常な事象で、このsuiteは結果を1回だけリトライなしで
待っていたため1回のmissで落ちた。

**修正済み。** sketchが12秒間隔で最大2回scanを再開し（`SCAN_RESTARTED n`を印字）、testの
待ち時間がその範囲を覆うようにした。これは回避策ではなくテストの修正である——RPAの件では
リトライが欠陥を隠すが、こちらは再開しても復活しないscanなら結局failするので、再現する欠陥を
隠すことはない。何も取れなかったscanを再開するのはアプリケーションとしても普通の対処である。

この再開は**実機初回の実行で実際に発火した**（`LONG_VALUE_READY` → `SCAN_RESTARTED 1` →
`TARGET_FOUND`）。つまり最初の12秒の窓では一致する結果が本当に1件も来ておらず、元の失敗は
この環境で稀な事象ではないことになる。旧testがその回にfailしたはずだとまでは言えない
（旧30秒の窓なら後続のadvertisementを拾えた可能性がある）が、missそのものは明らかに珍しくない
——RPAの件とは違い、こちらはリトライが不具合を隠さない。

## 合格条件

- test codeがすべての入力を生成し、Serial assertionで結果を判定する。
- timeoutやretryを含む合否条件が固定されている。
- 公開APIどうしだけでなく、Arduino-ESP32同梱APIまたはraw ESP-IDF実装との組み合わせがある。
- EspBleと同名scenarioは同じwire期待値を使い、差分は`docs/API_PARITY.tsv`に理由付きで載っている。
- 手動確認が必要な項目を自動テストの合格条件へ混ぜない。
- 無指定の`pytest`が常設2台だけで完走する。
