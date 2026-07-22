# EspBleBluedroid

ESP32のBluedroidスタックを利用するArduinoライブラリです。NimBLEを利用する
兄弟ライブラリ[EspBle](../EspBle/)と、接続後のGATT操作を中心に似た使い勝手を
提供することを目標にしています。

現在は実装の最初の段階です。root lifecycle、Legacy Advertising、Scan、Central
1接続、非同期GATT Discovery / UUID・handle指定Characteristic操作 / Descriptor
Read / Write / Notification購読の
公開APIを実装し、
無印ESP32を2台使ったpeerテストで
検証しています。LE Secure Connections Just Works、静的passkey MITM、bondの
保存・列挙・削除も実装済みです。対話型pairingとBluetooth Classicは今後、
テストファーストで追加します。

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

セットアップと実行方法は[tests/README.ja.md](tests/README.ja.md)を参照してください。

公開APIとBluetooth Classicを共存させる設計方針は
[docs/API_DESIGN_POLICY.ja.md](docs/API_DESIGN_POLICY.ja.md)にまとめています。
実装済み範囲と未対応機能は[docs/STATUS.ja.md](docs/STATUS.ja.md)を参照してください。
実装は[開発方針](docs/DEVELOPMENT.ja.md)に従い、公開動作ごとにテストを先に追加します。

最小exampleのbuild:

```sh
arduino-cli compile --profile esp32 examples/CompileSmoke
```

公開APIの利用例は[examples/README.ja.md](examples/README.ja.md)にあります。

## 対象

- SoC: Classic Bluetoothを搭載する無印ESP32
- Arduino-ESP32: 3.3.10
- BLE backend: Bluedroid（NimBLE buildはテスト内で拒否します）
