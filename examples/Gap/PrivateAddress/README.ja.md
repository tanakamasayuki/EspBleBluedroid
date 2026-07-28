# PrivateAddress

> English: [README.md](README.md)

`EspBleConfig::ownAddressType`で、工場出荷のpublic addressの代わりにprivate addressを
使ってAdvertisingします。[Scan](../Scan/)または[ScanDump](../../Info/ScanDump/)を
動かした2台目の無印ESP32からaddress typeを確認できます。

## 2つのモード

sketch冒頭の`USE_RESOLVABLE_PRIVATE_ADDRESS`で切り替えます。

| | RandomStatic（既定） | ResolvablePrivate（RPA） |
|---|---|---|
| アドレス | `begin()`ごとに生成する固定random | controllerが周期的に変更 |
| 追跡耐性 | public addressは隠れるが固定値で追跡可能 | 回転するため追跡されにくい |
| bonding | 不要 | IRKによる再識別には必要 |
| `localAddress()` | 電波上の固定値を返す | 空文字列。現在RPAはcontroller内部で生成され取得APIがない |

Random Staticは先頭octetの上位2bitが`0b11`、RPAは`0b01`です。scannerからは
どちらも`EspBleAddressType::Random`として見えます。

## 主なAPI

- `EspBleConfig::ownAddressType`
- `EspBleOwnAddressType::Public`
- `EspBleOwnAddressType::RandomStatic`
- `EspBleOwnAddressType::ResolvablePrivate`
- `localAddress()` / `localAddressType()`

## 注意

- RPAはbondingで交換するIRKにより、アドレスが変わってもpeerが同じ機器だと解決できます。
- 無印ESP32ではRPA timeout変更APIが未対応のため、controller既定周期を使います。
- 設定処理はArduino wrapperだけに依存せず、Bluedroid GAP APIの完了eventまで確認してから
  Advertisingを開始します。

## 期待されるSerial出力

```text
Advertising with a random static address; current=ee:11:3b:79:fe:40 type=1
```
