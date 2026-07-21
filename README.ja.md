# EspBleBluedroid

ESP32のBluedroidスタックを利用するArduinoライブラリです。NimBLEを利用する
兄弟ライブラリ[EspBle](../EspBle/)と、接続後のGATT操作を中心に似た使い勝手を
提供することを目標にしています。

現在は実装の最初の段階です。まず、無印ESP32を2台使ってBluedroidのBLE接続を
自動検証するpeerテスト環境を用意しています。公開ライブラリAPIはまだ実装して
いません。

## 現在のテスト範囲

`tests/peer/stack_smoke`はArduino-ESP32同梱のBluedroid BLE APIを直接使い、
次を確認します。

- Advertisingとactive scan
- CentralからPeripheralへの接続
- GATT characteristicのreadとwrite
- CCCD購読とnotification
- 2台のSerial出力をpytestから検証

セットアップと実行方法は[tests/README.ja.md](tests/README.ja.md)を参照してください。

公開APIとBluetooth Classicを共存させる設計方針は
[docs/API_DESIGN_POLICY.ja.md](docs/API_DESIGN_POLICY.ja.md)にまとめています。
実装は[開発方針](docs/DEVELOPMENT.ja.md)に従い、公開動作ごとにテストを先に追加します。

最小exampleのbuild:

```sh
arduino-cli compile --profile esp32 examples/CompileSmoke
```

## 対象

- SoC: Classic Bluetoothを搭載する無印ESP32
- Arduino-ESP32: 3.3.10
- BLE backend: Bluedroid（NimBLE buildはテスト内で拒否します）
