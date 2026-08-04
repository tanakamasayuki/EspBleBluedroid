# EspBle相互接続テスト

> English: [README.md](README.md)
> 規則とscenario一覧: [../TEST_PLAN.ja.md](../TEST_PLAN.ja.md#espbleリリースパッケージとの相互接続suiteinterop)

このライブラリ（Bluedroid、無印ESP32）と兄弟ライブラリ
[EspBle](https://github.com/tanakamasayuki/EspBle)（NimBLE、ESP32-S3）の
cross-stack試験です。

同梱Bluedroidどうしの通信では、実装が「Bluedroidの癖」に依存していても両端で相殺されて
見えません。ここでは相手側を別のhost stackにします。

## fixture

| fixture | ボード | profile | firmware |
|---|---|---|---|
| `dut` | ESP32-S3 | `s3_peer_host` | 公開EspBleリリース（`sketch.yaml`で固定） |
| `peers["device"]` | 無印ESP32 | `esp32_peer_device` | このrepository |

EspBleは無印ESP32では動作しないため、EspBle側は常にNimBLE対応SoCになります。S3（EspBleの
メイン機材）は常設で、peerは`peer/`各suiteと同じ2台目の無印ESP32を使います。ボード間の配線は
不要で、Serialと給電だけを使います。

## 準備

手動での取得は不要です。EspBleのversionは各interop `sketch.yaml`で固定してあり、
Arduino CLIがそのreleaseをそのままinstallします。

```yaml
    libraries:
      - EspBle (1.1.0)
```

S3のportは他の2台と同じく`tests/.env`へ置きます。無指定の`pytest`にもこのsuiteが含まれます。

```dotenv
TEST_SERIAL_PORT_S3_PEER_HOST=/dev/ttyACM0
```

```sh
uv run --env-file .env pytest interop/
```

## 規則

- peer firmwareはArduino library index上の**公開release**を使い、`../EspBle`や
  default branch、未release commitは使いません。ここで確認するのは「実際にinstallできる
  version」との整合です。
- versionは`sketch.yaml`に置き、更新は別途のreleaseツールで行う明示的な変更として扱います。
  差分をreviewし、suite全体を再実行します。
- installされたpackageへpatchを当ててテストを通すことはしません。EspBle側を直さないと
  通らない場合は、その事実を結果に残します。
- build、flash、操作、期待値判定、timeout、cleanupまでpytestから無人実行できるscenarioだけを
  対象にします。スマートフォン操作、GUI確認、聴感評価はrelease checklistの手動相互運用へ
  分離します。

## scenario

| scenario | 内容 |
|---|---|
| [gatt_basic](gatt_basic/) | Bluedroid CentralとEspBle Peripheral。MTU 247交換、宣言propertyを含むDiscovery、Characteristic Read、応答あり/なしWrite、Descriptor Read/Write、Notification、確認応答を伴うIndication、購読解除、切断 |
| [advertise_scan](advertise_scan/) | Advertising / Scanの両方向。device name、manufacturer data、Service Data、Appearance、Tx Powerを一方のpayload builderで組み、相手のparserが同じfieldへ復元すること。同じadvertiserをpassive scanしたときはAdvertising payloadだけが見えること |
| [long_value](long_value/) | EspBle Peripheralが公開した300 byteの値を、MTU 247をまたいでUUID指定・handle指定の両方のReadで全体取得。全byteを相手のramp列と照合 |
| [duplicate_uuid](duplicate_uuid/) | EspBle Peripheralが同一Service内に置いた同一UUID Characteristic 2件を、Discoveryで区別し、UUID指定は1件目に届き、Read / Write / 購読 / Notificationがすべて両側でhandleに帰属することを確認。こちらのServer側が同じ形を拒否することも併記 |
| [security](security/) | Just Works、静的passkeyのPasskey Entry、Numeric Comparison（承認と拒否）。encrypted / authenticated / bonded / key sizeを両側でassertし、bondも両側で確認。attribute権限2段を各link種別で検証し、Numeric Comparisonでは2実装が同一の6桁を導出すること、拒否後は何も残らないことを確認 |
| [profile_wire](profile_wire/) | 共有codec headerの相互運用。FLOAT32をRead / Notificationで、CGMのE2E-CRCを一方が付与し他方が検証、SFLOATを逆方向のWriteで、iBeaconをadvertisementからdecode。役割を反転し、被検ライブラリがGATT Server兼beaconになる |

残りの予定scenario（接続系scenarioの逆方向、HID/MIDI）は内容とともに
[../TEST_PLAN.ja.md](../TEST_PLAN.ja.md#対象scenario実装が固まった順に追加)にあります。

## UUID

interop scenarioはテスト用UUID体系（`SSSSNNNN-b1dd-4d00-9e5a-627564726f69`）のうち
`01xx`のsuite tagを使います。両ライブラリのsuiteを同じ部屋で同時に走らせても衝突しません。
`gatt_basic`は`0100`、`advertise_scan`は`0101`（方向ごとに別UUIDを使い、どちらのscanner
も相手側のpayloadでは満たされないようにしています）、`long_value`は`0102`、
`duplicate_uuid`は`0103`、`security`は`0104`、`profile_wire`は`0105`です
（iBeacon payloadのUUIDも`0105 0100`なので、周囲の別beaconではbeacon scanが
満たされません）。

## logの読み方

EspBle側の出力は`ESPBLE_`で始まります。どちらのstackが出した行かがlog上で曖昧になりません。

## fixtureの注意点

- S3の`Serial`はboard既定のUART0のままにします。このfixtureのS3はCH9102の
  USB-serial bridge経由で接続しているため、「USB CDC On Boot」を有効にすると出力が
  native USB側へ移り、正常動作しているのに無言に見えます。
- EspBle側は起動行を待つのではなく`?`の状態要求に応答します。相手のボードをflashしている
  間に起動が終わるため、起動行だけをassertするとmonitorの読み出し開始タイミングに
  依存してしまいます。
