# Tests

`pytest-embedded`とArduino CLI backendを利用するEspBleBluedroidの実機テストです。

```text
peer/   無印ESP32 2台のBluedroid BLE / Classic接続自動テスト
```

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
| `peer/advertise_scan` | 公開APIのlifecycle、Advertising、payload超過拒否、Scan、値型result、`update()`配送 |
| `peer/advertise_payload` | raw AD構造、複数UUIDの集約、31 byte境界、時間指定停止 |
| `peer/connect_disconnect` | non-blocking接続、再接続ID、非同期失敗、`update()`配送、切断、再初期化 |
| `peer/gatt_client` | Database snapshot、UUID/handle指定Characteristic操作、Descriptor Read/Write、Notification購読/解除、切断時無効化、`update()`配送 |
| `peer/security_bond` | Just Works、暗号化GATT、bond保存、暗号化再接続、security callback、bond削除 |
| `peer/security_passkey` | 静的passkey MITM、passkey表示、authenticated GATT、bond保存 |
| `peer/runtime_passkey` | 実行時passkey入力、入力待ちの切断・終了、未回答timeout、再試行 |
| `peer/numeric_comparison` | Numeric Comparisonの確認・拒否・未回答timeout・再試行 |
| `peer/classic_inquiry` | dual-mode初期化、capability、Classic name / Class of Device / RSSI、停止・完了event |
| `peer/spp_server` | SPP Server、binary-safe双方向data、8件送信queue・overflow、再接続ID、remote切断、稼働中終了 |
| `peer/spp_client` | 非同期SPP Client、共通session、binary data、local切断、再接続、失敗/timeout |
| `peer/spp_receive_buffer` | 2048 byte固定長RX ring、binary read、overflow byte数、切断時無効化 |
| `peer/spp_stream` | Arduino Stream/Print、1000 byte分割write、flush、切断後の無効化 |
| `peer/spp_security` | Client/Server両roleのDisplayYesNo SSP、明示拒否、認証失敗後retry、Classic bond列挙・再接続・削除、認証・暗号化data |
| `peer/dual_mode_scan_spp` | active SPP session中のBLE Scan、Scan callbackからのbinary SPP往復 |

特定のテストだけを実行する場合はパスを追加できます。

```sh
uv run --env-file .env pytest peer/stack_smoke/ -v
```
