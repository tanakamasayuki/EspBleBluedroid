# Peer Tests

無印ESP32 2台をBLEで接続する自動テストです。ボード間の信号配線は不要です。

| pytest上の位置 | profile | 環境変数 | 現在のBLE role |
|---|---|---|---|
| 親側 | `esp32_peer_host` | `TEST_SERIAL_PORT_ESP32_PEER_HOST` | Central |
| `peer_device/` | `esp32_peer_device` | `TEST_SERIAL_PORT_PEER_DEVICE_ESP32_PEER_DEVICE` | Peripheral |

`stack_smoke`はライブラリの公開APIには依存せず、Arduino-ESP32同梱Bluedroid
APIだけで接続し、テスト環境と基本GATT data pathを検証します。

