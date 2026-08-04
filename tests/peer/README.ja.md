# Peer Tests

無印ESP32 2台をBLEで接続する自動テストです。ボード間の信号配線は不要です。

| pytest上の位置 | profile | 環境変数 | 現在のBLE role |
|---|---|---|---|
| 親側 | `esp32_peer_host` | `TEST_SERIAL_PORT_ESP32_PEER_HOST` | Central |
| `peer_device/` | `esp32_peer_device` | `TEST_SERIAL_PORT_PEER_DEVICE_ESP32_PEER_DEVICE` | Peripheral |

`stack_smoke`はライブラリの公開APIには依存せず、Arduino-ESP32同梱Bluedroid
APIだけで接続し、テスト環境と基本GATT data pathを検証します。

`advertise_scan`はEspBleBluedroidの公開APIを使い、lifecycle、Advertising、31 byte
超過の拒否、Scan resultと、利用者callbackが`update()`から配送されることを検証します。

`advertise_payload`は親側でraw PDUを解析し、複数Service UUIDのAD構造、31 byte境界、
時間指定停止を検証します。

`connect_disconnect`は公開APIによるCentral接続、再接続ごとのconnection ID、
到達不能peerの非同期失敗、callbackの`update()`配送、切断、stack再初期化を検証します。

`classic_inquiry`はdiscoverableなClassic-only peerに対するcapability snapshot、
Inquiry result、結果callbackからの停止、`update()`上の完了eventを検証します。

`spp_server`はraw ESP-IDF Clientに対する公開SPP Server、binary data、8件送信queue、
overflow、再接続ID、remote切断、stack終了を検証します。

`spp_client`はraw ESP-IDF Serverへの公開非同期Client接続、共通session、local切断、
再接続、失敗eventを検証します。

`spp_multi_backend`は公開APIに依存しないraw Bluedroidの成立性試験です。同一ACL上の
異なる2つのRFCOMM server channelへ同時接続し、handle別の双方向dataと両sessionの
切断を検証します。同じchannelへの2本目や公開APIの複数session対応を保証する試験では
ありません。

`dual_mode_scan_spp`は1つのdual-mode stack上でBLE Scanとbinary SPP trafficを
同時利用できることを検証します。
`multi_listener`は多重observer配送を検証します。primaryの`on*()`に加えてconnection系・
GATT Client系・GATT Server系eventの`add*Listener()`が登録順に配送されること、1 event
4件の上限、owner単位のlistener id、dispatch中に追加したlistenerがそのdispatchに
含まれないことを確認します。
`peripheral_connection`はraw Arduino-ESP32 BLE client相手に、Peripheral側の接続
lifecycleを検証します。接続event、こちらが観測するだけのMTU交換、パラメータを含む
`connection()` snapshot、その役割でのpairing、HCI切断理由、GATT Server eventが持つ
connection IDの一致を確認します。
