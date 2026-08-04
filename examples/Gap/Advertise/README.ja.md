# Advertise

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 2章「GAP編 — 探してつながる」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

デバイス名と16-bit Service UUID（HID、`1812`）を載せたconnectableなLegacy Advertisingを開始します。Peripheral側の最小例です。汎用BLEスキャナアプリか、2台目のボードで組み合わせる[Scan](../Scan/) exampleで確認できます。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（Peripheral）
- BLEスキャナ — 2台目のボードで[Scan](../Scan/) example、またはnRF Connect等のスキャナアプリ

## 動作

- デバイス名`EspBleBluedroid Advertiser`でスタックを初期化します
- local nameとHID Service UUID `1812`を、サイズごとに単一のComplete List AD構造へまとめてadvertiseします（CSS Part A 1.1）
- リセットするまでadvertiseし続けます。Centralからの接続要求はスタックが受け入れます

## 主なAPI

- `bluetooth.begin(config)` — スタック初期化。`config.deviceName`がGAPデバイス名になります
- `bluetooth.advertising().setName(name)` — advertising payloadへlocal nameを載せます
- `bluetooth.advertising().addServiceUuid(uuid)` — Service UUIDを掲載（サイズごとに単一のComplete Listへまとめます）
- `bluetooth.advertising().setChannelMap(mask)` — 使うadvertisingチャネルを絞ります（`EspBleAdvertisingChannel37/38/39` のビットマスク、0で3チャネルすべて）。Wi-Fiと重なるチャネルを避けられる代わりに、見つけてもらうまでの時間は延びます
- `bluetooth.advertising().start()` — connectableなLegacy Advertisingを開始。payloadが31 bytesを超える場合は`InvalidArgument`で失敗します
- `bluetooth.lastErrorName()` / `bluetooth.lastErrorDetail()` — 要求が拒否された理由

## 期待されるSerial出力

成功時は何も表示しません。失敗時:

```
BLE init failed: INVALID_STATE (...)
Advertising failed: INVALID_ARGUMENT (...)
```
