# Advertise

> English: [README.md](README.md)

Local Name、Service UUID、Manufacturer DataをconnectableなLegacy Advertisingで送信します。

## 必要なもの

- 無印ESP32 × 1
- [Gap/Scan](../Scan/)または汎用BLE scanner

## 動作

- `begin()`前にGAP device nameを設定します
- payloadへLocal Name、Battery Service UUID、Manufacturer Dataを追加します
- `start()`でAdvertisingを開始し、`update()`を継続して呼びます

## 主なAPI

- `EspBleConfig::deviceName` — stackのdevice name
- `advertising().setName()` — payloadのLocal Name
- `addServiceUuid()` / `setManufacturerData()` — 公開する識別情報とbinary値
- `setChannelMap()` — channel 37/38/39の利用範囲。`0`または`EspBleAdvertisingChannelAll`で全channel
- `advertising().start()` — payload検証と開始

同じbit幅のService UUIDは1つのComplete Listへまとめられます。Advertisingまたは
Scan Responseが31 byteを超えると`start()`は`InvalidArgument`で失敗します。

## 期待されるSerial出力

```text
advertising
```
