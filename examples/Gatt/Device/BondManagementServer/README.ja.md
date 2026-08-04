# BondManagementServer

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../../DIFFERENCES_FROM_ESPBLE.ja.md)

標準Bond Management Service（0x181E）のPeripheralです。Bond Management Feature（0x2AA5）は対応操作のread可能なuint24 bit field、Bond Management Control Point（0x2AA4）はwritableでop codeを`onWritten`で受け取ります。

## 必要なもの

- 1 × 無印ESP32（このスケッチ。Peripheral）
- 1 × Central: [BondManagementClient](../BondManagementClient/) example、または任意のBond Management client

## 動作

- `begin()`の前にControl PointとFeatureを登録し、0x181EをAdvertiseします
- 「Delete all bonds on server（LE）」（bit 10）対応を提示します → `0x000400`
- op code 0x03または0x06を受けたら、3秒後に**LEのbondをすべて**削除するよう予約します（Client側の切断を先に済ませるため）
- 削除は`loop()`から実行し、消えた件数を表示します

## なぜ「要求元1件」ではなく全件なのか

このServiceは「要求してきた機器のbondを削除」と「全bondを削除」を区別します。前者を
実現するには**Control Pointを書いたpeerのアドレス**が必要ですが、このライブラリでは
Peripheral linkにconnection snapshotが無く、そのアドレスを取得できません
（[docs/STATUS.ja.md](../../../../docs/STATUS.ja.md)参照）。

そのため、このsketchは実行できることだけを申告します。Feature bit fieldでは
「Delete all bonds on server（LE）」を立て、要求元単位のbitは立てません。仕様に沿った
Clientはまずこのfeatureを読み、申告されていない操作は要求しません。

## 主なAPI

- `bluetooth.gattServer().onWritten(...)` — Control Pointのop codeを受信します。`write.connectionId`はPeripheral linkを表しますが、そのアドレスは含みません
- `bluetooth.bondCount()` / `bluetooth.bond(i, bond)` / `bluetooth.deleteBond(bond)` / `bluetooth.deleteAllBonds()` — bondの列挙と削除

## メモ

- **削除はWriteのcallbackではなく`loop()`から行ってください。** bondの削除はBluedroidの非同期な永続ストアが落ち着くまで待つため、イベント配送の中で行うべきではありません。
- op codeには認可コードが追加octetとして付くことがあります。このexampleはop code単体のみを受け付け、それはFeatureの「with authorization」bitを立てていないことと整合します。
- Bluetooth Classicのbondは別のstore（`bluetooth.classic().deleteAllBonds()`）です。LE用のop codeがそちらへ影響してはいけないため、このexampleはBLE側の`deleteAllBonds()`だけを呼びます。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| 要求元peerの特定 | `onConnected()`／`onDisconnected()`でPeripheral linkの`peerAddress`が得られる | **できない** — Peripheral linkにconnection snapshotが無い |
| op code 0x03の対象範囲 | そのpeerのbondのみ | LEのbond全件 |
| Feature bit field | `0x000011`（要求元単位のbit） | `0x000400`（Delete all bonds on server、LE） |
| 削除を実行するタイミング | `onDisconnected()`から | Write後3秒、`loop()`から |

**なぜ違うのか:** 削除はpeer addressで対象を絞る必要があり、着信linkのアドレスはまさにEspBleBluedroidがまだ公開していないものです。実装できない機能を申告するのは範囲を狭めるより悪いため、このexampleはFeature bitを実挙動に合わせて狭めています。

**移植のしかた:** `onDisconnected()`駆動でアドレス指定していた削除を、タイマ駆動の`deleteAllBonds()`へ置き換え、Featureの値を合わせます。Service、2つのCharacteristic、`onWritten()`でのop code解釈といった残りの部分はEspBleと同じです。

## 期待されるSerial出力

```
Bond Management op code: 3
Deleting LE bonds in 3000 ms
Deleted 1 bond(s); remaining=0
```
