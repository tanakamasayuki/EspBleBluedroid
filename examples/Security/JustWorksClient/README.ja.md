# JustWorksClient

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 3章「セキュリティ編」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

[JustWorksServer](../JustWorksServer/)のCentral側です。接続して**Just Works**（LE Secure Connections、passkeyなし）+ Bondingでpairingし、暗号化linkを要求するCharacteristicをReadします。

Just Worksは、**どちらの側もpasskeyを表示も入力もできない**とき（両側の`ioCapability`が`None`）に選ばれる方式です。MITM保護を伴わない暗号化が得られます。傍受は防げますが、pairing中に割り込む能動的な中間者は防げません。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（Central）
- 暗号化Characteristicを持つBLE Peripheral × 1 — 2台目のボードで[JustWorksServer](../JustWorksServer/)、またはスマートフォンアプリ

## 動作

- ServerのService UUIDをActive Scanで探し、最初に見つかった相手へ接続します
- `pairOnConnect`によりlink成立と同時にpairingが始まります。ユーザー操作はありません
- Securityの結果と、保存された最初のbondを表示します
- Security確立後、暗号化CharacteristicをDiscoveryしてReadします
- `c`で切断中に全Bondを削除し、残数を表示します

## 主なAPI

- `EspBleSecurityConfig` — `enabled`、`bonding`、`pairOnConnect`。`ioCapability`は`None`のまま
- `bluetooth.onSecurityChanged(callback)` — `update()`から配送。`success`とConnectionのsecurity snapshot（`encrypted`、`authenticated`、`bonded`、`encryptionKeySize`）
- `bluetooth.bondCount()` / `bluetooth.bond(index, out)` / `bluetooth.deleteAllBonds()` — BLEのbond store
- `bluetooth.requestSecurity(connectionId)` — `pairOnConnect`の代わりに明示的にpairingを開始する方法（[StaticPasskeyClient](../StaticPasskeyClient/)がこの形）

## メモ

- **成功しても`authenticated`は0のままです。** これはJust Worksの仕様で、失敗ではありません。attributeに`authenticatedRead`／`authenticatedWrite`を要求すると、この方式では到達できません。passkey方式を使ってください（[StaticPasskeyClient](../StaticPasskeyClient/)、[RuntimePasskeyClient](../RuntimePasskeyClient/)）。
- **bond済みの再接続では、pairingせずに暗号化されます。** 保存済みの鍵が使われるため、ユーザーに見える手順なしで`onSecurityChanged()`が成功を報告します。最初からやり直すには**両側**でbondを削除してください。
- **Bondの削除は切断中に行ってください。** `deleteBond()`／`deleteAllBonds()`はBluedroidの非同期な永続ストア更新を待ってから戻ります。
- BLEのbondとBluetooth Classicのbondは別のstoreです。Classic側は`bluetooth.classic().bondCount()`などになります。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| このexample自体 | 無い（EspBleはServer側からJust Worksを扱う） | こちらで提供。security eventが配送されるのはCentral側だから |
| Central linkでの`onSecurityChanged()` | 配送される | 同じ |
| bond storeのAPI | `bondCount()`／`bond()`／`deleteBond()`／`deleteAllBonds()` | 同じ。加えてClassic用の別store（`classic()`） |

**なぜ違うのか:** このライブラリではpairingのPeripheral側にsecurity eventがまだ届かないため、一連の流れをそのまま見せられるのはCentral側のsketchになります（[Security/README.ja.md](../README.ja.md)参照）。

**移植のしかた:** sketchの中身はEspBleと1対1で対応します。変わるのはクラス名とインスタンス名だけです。

## 期待されるSerial出力

```
Send 'c' while disconnected to clear all bonds.
Connected: 1
Security: success=1 encrypted=1 bonded=1 key=16
Stored bond: d0:cf:13:58:fd:95
Encrypted value: encrypted value
```
