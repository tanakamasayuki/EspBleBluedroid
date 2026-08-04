# BondManagementClient

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../../DIFFERENCES_FROM_ESPBLE.ja.md)

Bond Management Service（0x181E）へ接続し、Bond Management Feature bit fieldをRead、Bond Management Control Pointへ「Delete bond of requesting device（LE）」（0x03）を応答ありWriteします。

## 必要なもの

- 1 × 無印ESP32（このスケッチ。Central）
- 1 × Bond Management Peripheral: [BondManagementServer](../BondManagementServer/) example

## 動作

- 0x181EをAdvertiseする機器をscanして接続
- Bond Management Feature（0x2AA5）をReadし、対応操作のbit fieldを表示
- Bond Management Control Point（0x2AA4）へ op code 0x03（Delete bond of requesting device、LE）を**応答ありWrite**

## 主なAPI

- `bluetooth.writeCharacteristic(connectionId, service, characteristic, data, length, true)` — 応答ありWrite

## メモ

- **op codeは「何を削除するか」、Feature bit fieldは「Serverが何をできるか」を表します。** まずFeatureを読んでください。「delete bond of requesting device」を申告していないServerは0x03を実行できません。ここの[BondManagementServer](../BondManagementServer/) exampleは`0x000400`（delete all bonds on server、LE）を申告し、0x03に対してはLEのbondを全件削除します。このライブラリのPeripheralでは要求元peerを特定できないためです。
- 応答ありWriteの成功は「Serverがop codeを受理した」ことを示すもので、bondが削除済みであることは意味しません。削除は通常、切断後まで遅延されます。

## 期待されるSerial出力

```
Bond Management Feature: 0x000400
Delete-bond op code sent
```
