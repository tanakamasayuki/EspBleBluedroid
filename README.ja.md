# EspBleBluedroid

ESP32のBluedroidスタックを利用するArduinoライブラリです。NimBLEを利用する
兄弟ライブラリ[EspBle](https://github.com/tanakamasayuki/EspBle)と、接続後のGATT操作を中心に似た使い勝手を
提供することを目標にしています。

現在は実装の最初の段階です。root lifecycle、Legacy Advertising、Scan、Central
1接続、非同期GATT Discovery / UUID・handle指定Characteristic操作 / Descriptor
Read / Write / Notification購読の
公開APIを実装し、
無印ESP32を2台使ったpeerテストで
検証しています。LE Secure Connections Just Works、静的・実行時passkey MITM、
Numeric Comparison、bondの保存・列挙・削除も実装済みです。Bluetooth Classicは
capability snapshotと`classic().inquiry()`を実装しています。SPPはClient/Server共通の
session、binary-safeな双方向data、送信完了event、Server/Client sessionへ自動追従する
`EspBluedroidSppSerial`、切断・再接続、
SSP Numeric Comparisonによる認証・暗号化、BLE bondとは分離したClassic bondの
列挙・削除、DisplayOnly/KeyboardOnly Passkey Entryまで利用できます。secure SPPは
Client/Server両roleを実機確認済みです。
他profileは今後もテストファーストで追加します。

## 現在のテスト範囲

peerテストでは次を確認します。

- Advertisingとactive scan
- CentralからPeripheralへの接続
- GATT characteristicのreadとwrite
- Discoveryで得たhandleを使うcharacteristic read/write/subscribe/unsubscribe
- GATT descriptorのbinary-safeなreadとwrite
- Just Works pairing、暗号化再接続、BLE bond管理
- DisplayOnly/KeyboardOnlyの静的passkey MITMとauthenticated GATT
- CCCD購読とnotification
- 2台のSerial出力をpytestから検証
- 公開APIによるAdvertising/Scanと、`update()` contextからのcallback配送
- Classic capabilityとInquiry、name / Class of Device / RSSI、停止完了
- Classic SPP Client/Server、Serial形式Stream API、SSP拒否・retry・bond再接続・認証暗号化data
- Classic DisplayOnly/KeyboardOnly Passkeyの表示・入力とI/O capability変更再初期化
- active SPP sessionとBLE GATT Discovery / Read / Write / Notificationの同時利用

セットアップと実行方法は[tests/README.ja.md](tests/README.ja.md)を参照してください。

公開APIとBluetooth Classicを共存させる設計方針は
[docs/API_DESIGN_POLICY.ja.md](docs/API_DESIGN_POLICY.ja.md)にまとめています。
EspBle（NimBLE）との現在のBLE API差分は
[docs/BLE_BACKEND_DIFFERENCES.ja.md](docs/BLE_BACKEND_DIFFERENCES.ja.md)を参照してください。
利用方法は[BLE通信の入門ガイド](docs/GUIDE_BLE_BASICS.ja.md)と
[Bluetooth Classic通信の入門ガイド](docs/GUIDE_CLASSIC_BASICS.ja.md)を参照してください。
実装済み範囲と未対応機能は[docs/STATUS.ja.md](docs/STATUS.ja.md)を参照してください。
実装は[開発方針](docs/DEVELOPMENT.ja.md)に従い、公開動作ごとにテストを先に追加します。

最小exampleのbuild:

```sh
arduino-cli compile --profile esp32 examples/CompileSmoke
```

公開APIの利用例は[examples/README.ja.md](examples/README.ja.md)にあります。

## 対象

- SoC: Classic Bluetoothを搭載する無印ESP32
- Arduino-ESP32: 3.3.11
- BLE backend: Bluedroid（NimBLE buildはテスト内で拒否します）
