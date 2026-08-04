# Tests

> English: [README.md](README.md)
> 何が検証済みで、何が空白で、どの順に埋めるか: [TEST_PLAN.ja.md](TEST_PLAN.ja.md)

`pytest-embedded`とArduino CLI backendを利用するEspBleBluedroidの実機テストです。

```text
unit/     backend非依存のcodec、parser、状態変換（実機不要）
peer/     無印ESP32 2台のBluedroid BLE / Classic接続自動テスト
interop/  無印ESP32＋ESP32-S3（EspBleリリースパッケージ）のcross-stack試験（予定）
```

層の分け方、fixture、カバレッジ表、優先順位は[TEST_PLAN.ja.md](TEST_PLAN.ja.md)にあります。
この文書は実行手順です。

## セットアップ

Arduino CLIへ`esp32:esp32` 3.3.11をインストールしたうえで、次を実行します。

```sh
cd tests
cp .env.example .env
uv sync
```

`.env`はGit管理されません。別のPCやUSB接続順でポートが変わった場合は、各環境の
`.env`だけを編集してください。テストコードや`sketch.yaml`の変更は不要です。

初期設定では次の2台を使用します。

```dotenv
TEST_SERIAL_PORT_ESP32_PEER_HOST=/dev/ttyUSB0
TEST_SERIAL_PORT_PEER_DEVICE_ESP32_PEER_DEVICE=/dev/ttyUSB1
```

`host`と`device`はpytest fixture上の識別名で、BLE roleではありません。現在の
各テストでは親側をCentral、`peer_device/`側をPeripheralに固定しています。

## 実行

```sh
uv run --env-file .env pytest
```

テストは両方のESP32へsketchを書き込みます。既存のfirmwareは上書きされます。
成功時はAdvertising/Scan、接続、GATT characteristic/descriptor read/write、notificationと、双方のSerial
監視が動作したことを意味します。

| suite | 確認範囲 |
|---|---|
| `peer/stack_smoke` | Arduino-ESP32同梱APIによる接続、GATT read/write、CCCD、notification |
| `peer/advertise_scan` | 公開APIのlifecycle、Advertising/Scan Response二面構成、Service Data・Appearance・Tx Powerを含むactive Scan merge、payload超過拒否、値型result、`update()`配送 |
| `peer/advertise_payload` | raw AD構造、複数UUIDの集約、31 byte境界、時間指定停止 |
| `peer/ibeacon` | EspBle共通codecでのiBeacon encode、broadcast、scan、decode |
| `peer/connect_disconnect` | non-blocking接続、再接続ID、MTU交換、HCI切断理由、非同期失敗、`update()`配送、切断、再初期化 |
| `peer/connection_parameters` | 初期connection parameter snapshot、更新要求、両peerの合意値、`update()`配送 |
| `peer/local_identity` | Random Static / RPA、現在アドレス、−12/+9 dBmと電波上のTx Power |
| `peer/accept_list` | 初期化前拒否、重複登録、一覧外Centralの接続拒否、Scan側`acceptListOnly`、`Any`変更後の接続と即時切断 |
| `peer/directed_advertising` | 宛先CentralへのpayloadなしHigh Duty、接続・切断、1.28秒自動停止、Low Duty継続・明示停止 |
| `peer/gatt_client` | Database snapshot、Characteristic単体探索、UUID/handle指定Characteristic操作、Descriptor handle Read/Write、Notification購読/解除、切断時無効化、`update()`配送 |
| `peer/gatt_server` | 静的GATT Server、動的Read、binary Write、Descriptor、CCCD購読、Notification、Indication（CCCD 0x0002と確認応答後の完了）、`update()`配送 |
| `peer/gatt_disconnect_purge` | 実行中Read中の`disconnect()`受理、完了が1件だけ届くこと、drop 0、再接続後のDiscovery/Read |
| `peer/duplicate_uuid` | Server側の重複Characteristic / Descriptor UUID拒否と不正UUID拒否（error名・detail文字列）、別Serviceの同一UUID受理、Client側のhandle指定Read・購読とNotificationの経路分離 |
| `peer/long_value` | MTUを超える値のRead。UUID指定・handle指定の両方で全体が返り、内容がpeerのrampと一致すること |
| `peer/service_changed` | Generic Attribute 0x1801 / Service Changed 0x2a05をstackが公開すること（applicationは登録しない） |
| `peer/security_bond` | Just Works、暗号化GATT、bond保存、暗号化再接続、security callback、bond削除 |
| `peer/security_passkey` | 静的passkey MITM、passkey表示、authenticated GATT、bond保存 |
| `peer/runtime_passkey` | 実行時passkey入力、入力待ちの切断・終了、未回答timeout、再試行 |
| `peer/numeric_comparison` | Numeric Comparisonの確認・拒否・未回答timeout・再試行 |
| `peer/classic_inquiry` | dual-mode初期化、capability、Classic name / Class of Device / RSSI、停止・完了event |
| `peer/spp_server` | SPP Server、binary-safe双方向data、8件送信queue・overflow・送信完了、再接続ID、remote切断、稼働中終了 |
| `peer/spp_client` | 非同期SPP Client、共通session、binary data・送信完了、local切断、再接続、失敗/timeout |
| `peer/spp_multi_backend` | raw Bluedroid、同一ACL上の異なる2 SCN、2 session同時接続、handle別双方向data、両session切断 |
| `peer/spp_receive_buffer` | 2048 byte固定長RX ring、binary read、overflow byte数、切断時無効化 |
| `peer/spp_serial` | root bindの`EspBluedroidSppSerial`、2つの連続Server sessionへの自動追従、Stream/Print、1000 byte分割write、flush、切断後の無効化 |
| `peer/spp_security` | Client/Server両roleのDisplayYesNo SSP、明示拒否、認証失敗後retry、Classic bond列挙・再接続・削除、認証・暗号化data |
| `peer/spp_passkey` | Classic DisplayOnly/KeyboardOnlyの両方向Passkey表示・入力、未回答timeout・遅延入力拒否・retry・入力待ち終了、認証・暗号化SPP、I/O capability変更再初期化 |
| `peer/a2dp_sink` | 公開A2DP Sink/AVRCP Controllerとraw Source/Target、PCM、Play Press/Release、absolute volume、callback context、切断・終了 |
| `peer/a2dp_source` | 公開A2DP Source/AVRCP Targetとraw Sink/Controller、PCM、Pause Press/Release、absolute volume、callback context、切断・終了 |
| `peer/hfp_backend` | 公開HFP Hands-Free/Audio Gateway間のSLC、SCO、CVSD/mSBC mono PCM双方向data、切断 |
| `peer/dual_mode_scan_spp` | active SPP session中のBLE Scan・GATT接続、Discovery、Read/Write、同一接続で64→128→256通知、round別BLE event drop集計、配送済み通知のSPP往復・RX ring保持、満杯時のGATT完了優先配送 |

特定のテストだけを実行する場合はパスを追加できます。

```sh
uv run --env-file .env pytest peer/stack_smoke/ -v
```

## EspBle相互接続suite（`interop/`、実装中）

EspBle（NimBLE）との相互接続を他stack試験としてこの`tests/`へ置きます。兄弟directoryや
開発branchは参照せず、versionとchecksumを固定した公開済みEspBleリリースパッケージを
peer firmwareの依存に使用します。

EspBleは無印ESP32では動作しないため、2台目はESP32-S3（profile `s3_peer_device`）です。
そのportを設定するとsuiteが有効になり、未設定なら自動skipします。したがって無指定の
`pytest`は常設の無印ESP32 2台だけで完走します。

```dotenv
TEST_SERIAL_PORT_PEER_DEVICE_S3_PEER_DEVICE=/dev/ttyUSB2
```

```sh
uv run --env-file .env pytest interop/
```

追加対象は、build、flash、接続操作、期待値判定、timeout、cleanupまでpytestから自動実行できる
scenarioだけです。スマートフォン、GUI、聴感確認など手動操作を必要とするものはこのsuiteへ
含めません。対象versionの更新は明示的な変更としてreviewし、自動でlatest releaseへ追従させません。

scenario一覧、パッケージ固定の規則、skipの意味は
[TEST_PLAN.ja.md](TEST_PLAN.ja.md#espbleリリースパッケージとの相互接続suiteinterop)にあります。
