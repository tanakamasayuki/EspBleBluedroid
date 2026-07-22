# Tests

`pytest-embedded`とArduino CLI backendを利用するEspBleBluedroidの実機テストです。

```text
peer/   無印ESP32 2台のBluedroid BLE接続自動テスト
```

## セットアップ

Arduino CLIへ`esp32:esp32` 3.3.10をインストールしたうえで、次を実行します。

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
成功時はAdvertising/Scan、接続、GATT read/write、notificationと、双方のSerial
監視が動作したことを意味します。

| suite | 確認範囲 |
|---|---|
| `peer/stack_smoke` | Arduino-ESP32同梱APIによる接続、GATT read/write、CCCD、notification |
| `peer/advertise_scan` | 公開APIのlifecycle、Advertising、payload超過拒否、Scan、値型result、`update()`配送 |
| `peer/advertise_payload` | raw AD構造、複数UUIDの集約、31 byte境界、時間指定停止 |

特定のテストだけを実行する場合はパスを追加できます。

```sh
uv run --env-file .env pytest peer/stack_smoke/ -v
```
