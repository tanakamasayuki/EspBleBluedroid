# EspBleとの違い

> English: [DIFFERENCES_FROM_ESPBLE.md](DIFFERENCES_FROM_ESPBLE.md)

このディレクトリのexampleは、兄弟ライブラリ
[EspBle](https://github.com/tanakamasayuki/EspBle)から移植したものです。EspBleは
BLE backendがNimBLEのESP32 SoC（S3 / C3 / C6 / H2 / P4）を対象にしています。
EspBleBluedroidは、ファミリの中で唯一Bluetooth Classicを持つ**無印ESP32**を、
Arduino-ESP32同梱のBluedroid backend経由で対象にします。

両ライブラリに存在するBLE機能は意図的に同じAPIにしてあるため、ほとんどのexampleは
名前の置き換えだけで移植できます。このページには、ライブラリ全体に共通する違いを
まとめます。**個々のexampleの書き方が変わる違いは、そのexampleのREADME**の
「EspBleとの違い」／「Differences from EspBle」に、理由と移植手順つきで書いています。

## 常に当てはまる違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| クラス／インスタンス | `EspBle ble;` | `EspBleBluedroid bluetooth;` |
| ヘッダ | `#include <EspBle.h>` | `#include <EspBleBluedroid.h>` |
| バージョンマクロ | `ESPBLE_VERSION_STR` | `ESPBLEBLUEDROID_VERSION_STR` |
| `sketch.yaml`のprofile | `esp32s3`（既定）、`esp32c3`、`esp32c6`、`esp32h2`、`esp32p4` | `esp32`のみ |
| ビルドコマンド | `arduino-cli compile --profile esp32s3 …` | `arduino-cli compile --profile esp32 …` |

次の4つのcodec headerは両ライブラリで等価です。一方がencodeした値を他方がdecode
できます。`EspBleUuid.h`、`EspBleIBeacon.h`、`EspBleMedicalFloat.h`、
`EspBleCgmCrc.h`。`EspBleUuid.h`は完全一致で、残る3つは整形だけが違い、演算とテーブルは
同一です（[docs/ESPBLE_FEEDBACK.ja.md](../docs/ESPBLE_FEEDBACK.ja.md)）。移植で書き換える
ものは何もありません。

## こちらには無いもの

| 領域 | 無いもの | なぜ | 影響するexample |
|---|---|---|---|
| 無線 | LE 2M / Coded PHY、`updatePhy()`、`onPhyUpdated()`、`EspBleConnection::txPhy`／`rxPhy` | 無印ESP32の無線はBluetooth 4.2 LEで、切り替える先のPHYが無い | [Gap/ConnectionParameters](Gap/ConnectionParameters/) |
| Advertising | Extended / Periodic Advertising | controllerがLegacy Advertisingのみに対応 | — |
| Central | 同時2接続以上、`disconnect(id, reason)` | 直接GATTC移行中で、peer testが固定しているのは1 link。wrapperがlocal reasonをlink終了へ渡さない | [Gap/Connect](Gap/Connect/) |
| Peripheral | **着信**linkに対する`onConnected()`／`onDisconnected()`／`bluetooth.connection()`、およびPeripheral単体機器でのBLE security event | Peripheral connection snapshotをまだ公開していない（[docs/STATUS.ja.md](../docs/STATUS.ja.md)参照） | [Gap/AcceptList](Gap/AcceptList/)、[Gap/DirectedAdvertising](Gap/DirectedAdvertising/)、[Gap/PrivateAddress](Gap/PrivateAddress/)、[Gatt/Device/BondManagementServer](Gatt/Device/BondManagementServer/)、[Security/](Security/) |
| GATT Client | `setAutoReconnect()`、`EspBleConfig::persistentSubscriptions`、`setAutoRediscover()` | linkを跨ぐhandle単位の状態をpeer testで固定できていない | [Gatt/Basics/AutoReconnectClient](Gatt/Basics/AutoReconnectClient/) |
| HID Hostのkeyboard event | decode済みkeyboard eventの`rawData` / `rawLength`が空のまま | 本ライブラリはdecode元のreportを載せるので、`event.rawLength`を読むsketchはこちらで8、あちらで0を見る（`tests/interop/hid`で判明。同testはこちら側だけでassertしている。要望は[docs/ESPBLE_FEEDBACK.ja.md](../docs/ESPBLE_FEEDBACK.ja.md)） | [Hid/](Hid/) |
| プラットフォーム | ESP-HostedのSDIO pin指定 | 無印ESP32は自前の無線を持ち、ESP-Hostedのhostになることがない | EspBleの`Hosted/CustomPins`に対応するexampleは無い |

### Serverを組む前に知っておきたい帰結

Bluedroidは接続が成立するとadvertisingを止め、このライブラリはPeripheral側の切断
eventを配送しません。そのため、sketch自身がadvertisingを再開しない限り、
**Server exampleは1 bootで1回しか接続を受け付けません**。選択肢は概ね次の順で扱いやすい
です。

| 方法 | トレードオフ |
|---|---|
| コマンドやボタンで再開する（`advertising().start()`） | 明示的で安全。次のpeerを受け入れる時点をsketchが決められる |
| 最後のGATT操作からの経過時間で再開する | 無人運用できるが、「相手がいなくなった」判定のヒューリスティックが必要 |
| ボードをリセットする | 立ち上げ確認には十分。製品向けではない |
| `isAdvertising()`を監視してfalseなら再開する | **非推奨**。`isAdvertising()`は接続*中*もfalseになるため、接続中にadvertiseしてしまい、単一接続のServerが扱えない2台目を受け入れる可能性がある |

このディレクトリのGapとSecurityのPeripheral exampleは1番目の方法を取り、sketch中に
その旨を書いています。

## APIは同じで、タイミングや文言が違うもの

| 領域 | こちらの挙動 | 影響するexample |
|---|---|---|
| MTU交換 | Bluedroidが自身のcallback内からの要求を拒否するため、接続worker完了後の最初の`update()`から開始する。`onConnected()`時点は23のまま | [Gap/Mtu](Gap/Mtu/) |
| Scan結果 | BluedroidがPDUごとに1 eventを上げるため、同じaddressのAdvertisingとScan Responseをライブラリがmergeしてから配送する。結果queueは16件 | [Gap/Scan](Gap/Scan/)、[Info/ScanDump](Info/ScanDump/) |
| RPA | `localAddress()`は空の`String`を返す。controllerが現在のRPAを公開しない | [Gap/PrivateAddress](Gap/PrivateAddress/) |
| GATT Clientの操作 | 1接続につき同時1操作 | すべての`…Client` example |
| GATT ClientのRead | 1回のATT応答に収まらない値も全体が返る。Bluedroidが内部で読みを継続するため、EspBleと同じく`result.value`へ結合済みの全体が入る（`tests/peer/long_value`で実機確認） | [Gatt/Basics/Client](Gatt/Basics/Client/)ほかすべての`…Client` |
| エラー詳細文字列 | 文言が異なる（例: `name does not fit in legacy scan response payload`） | [Gap/ScanResponse](Gap/ScanResponse/) |

両ライブラリで**同じ**リソース上限（移植時に考える必要がないもの）: Advertisingは
payloadごとにService UUID 4件・Service Data 4件、Scan結果はUUID 8件・Service Data
4件、GATT ServerはService 8・Characteristic 32・Descriptor 16、Discovery snapshotは
16 / 48 / 48、accept listは8件、希望MTUの既定は247。

## こちらにしか無いもの

EspBleBluedroidにはEspBleが持てないBluetooth Classicがあります。

- [Classic/](Classic/) — Inquiry、SPP（Server / Client / `Stream` wrapper / Security / Passkey）、A2DP Sink・Source、HFP Hands-Free・Audio Gateway、profile対応表
- [DualMode/](DualMode/) — BLEとClassicの同時通信

## 現在の実装状況

実装済み範囲と上記の制限の一次情報は[docs/STATUS.ja.md](../docs/STATUS.ja.md)です。
EspBleと共有するBLE APIの方針は
[docs/API_DESIGN_POLICY.ja.md](../docs/API_DESIGN_POLICY.ja.md)、backendの比較は
[docs/BLE_BACKEND_DIFFERENCES.ja.md](../docs/BLE_BACKEND_DIFFERENCES.ja.md)に
あります。
