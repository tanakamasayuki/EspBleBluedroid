# VendorHost

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

vendor定義のHIDデバイス向けBLE HID Host（Central）です。HID Service `0x1812` をscanし、Pairing・Discovery後にVendor Input Reportを受信し、Vendor Output / Feature Reportを書き込みます。security完了後の `discover()` フローはほかのHID種別とすべて同じです。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（HID Host / Central）
- HIDデバイス × 1: [VendorDevice](../VendorDevice/)を動かす2台目のボード

## 動作

- HID Service `1812` をadvertiseする最初の機器をscanして接続します
- preferred MTUに100を要求し、Bondingつきでsecurityを有効化します
- 暗号化成功後に `onSecurityChanged` からHID Discoveryを開始します
- 受信したVendor Input Reportを `onVendorInput()` で表示します（Report ID、length、raw bytes）
- `o` で8byteのVendor Output Report、`f` で8byteのVendor Feature Reportを書き込み（接続中のみ）

## 主なAPI

- `bluetooth.hidHost().discover(connectionId)` — security完了後にHID Discoveryを開始
- `hidHost().onDiscovered(cb)` — `success` / `detail` を持つ `EspBleHidKeyboardHostDiscovery`
- `hidHost().onVendorInput(cb)` — `reportId`、`rawLength`、`rawData` を持つ `EspBleHidVendorInputEvent`
- `hidHost().sendVendorOutput(connectionId, data, length)` / `sendVendorFeature(connectionId, data, length)` — デバイスへ書込み（成否を返す）

## メモ

- **reportは1つのATT payload（`MTU - 3`）に収まる必要があります。** そのための設定が `preferredMtu` で、既定63 byteのvendor reportは既定MTU 23では到底足りません。`onDiscovered()` 後に書くなら心配は要りません。Discoveryは何往復もするので、MTU交換はすでに終わっています。
- **OutputとFeatureのhandleはDiscoveryから得られます。** Report Reference descriptorから読むので、`onDiscovered()` 前の `sendVendorOutput()` には書き込む先がありません。Discoveryを自前で追わないなら `hidHost().ready(connectionId)` を見ます。
- `sendVendorOutput()` / `sendVendorFeature()` は、lengthがデバイスの宣言サイズと違えば `InvalidArgument`、そのreportが無ければ `NotFound` でfalseを返します。区別するには `lastErrorName()` を表示します。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| クラス・メソッド・callback名 | `ble.hidHost()` | 同じ |
| Discovery | GATT操作の列として発行 | APIは同じ。内部では1操作ずつ。この後端はlinkあたり1つのCentral GATT操作しか許さないため |
| MTU交換 | 接続時に要求 | 接続workerの完了後、最初の `update()` から開始する。Bluedroidは自身のcallback内からの要求を拒否するため。`onConnected()` の時点では23 |
| 同時に扱えるデバイス | 複数接続 | 同時に1link |

**移植方法:** ライブラリオブジェクトの宣言を変えるだけです。

## 期待されるSerial出力

```
Send 'o' for Output or 'f' for Feature after discovery.
HID discovery: ready
Vendor Input report=6 length=8 data=45 53 50 00 04 05 06 07
Output: sent
```
