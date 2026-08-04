# SppSecurity

> English: [README.md](README.md)
> 概念の説明: [Bluetooth Classic通信の入門ガイド](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) Securityの章
> EspBle: 対応exampleなし — Bluetooth Classic専用（[DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)）

**認証とlink暗号化を要求する**SPP serverを、Secure Simple Pairing（SSP）の**Numeric Comparison**と組み合わせる例です。両方の機器が同じ6桁を表示し、人が一致を確認します。

Classicのsecurityは意図的に2か所に分かれています。

| 場所 | 決めること |
|---|---|
| `EspBleConfig::classicSecurity` | この機器のpairingのしかた。I/O capabilityと応答timeout。機器全体で1回設定します |
| `EspBluedroidSppServerConfig::security` | このSPP serviceが要求する水準。なし／認証／認証＋暗号化 |

分けておくことで、将来のClassic profileがpairingのUIを共有しつつ、SPPの方針を引き継がずに済みます。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（secure SPP server）
- `DisplayYesNo`の相手 × 1 — スマートフォン（pairingダイアログに6桁が出ます）、または同じ構成の2台目のボード

比較そのものが目的なので、両方の画面を見える状態にしてください。

## 動作

- `begin()`の前に`ioCapability = DisplayYesNo`でClassic securityを有効にします
- `security`が`AuthenticatedEncrypted`のSPP serverを開始します
- 比較用の6桁をpeer address付きで表示し、Serialから`y` / `n`を待ちます
- `confirmNumericComparison(peerAddress, accept)`で回答します
- 認証結果を表示し、続いてsessionの`authenticated` / `encrypted`を表示します
- 暗号化されたsession上で受信データをechoします

## 主なAPI

- `EspBleConfig::classicSecurity` — `enabled`、`ioCapability`、`responseTimeoutMilliseconds`（既定30000）
- `EspBluedroidSppSecurity` — `None`、`Authenticate`、`AuthenticatedEncrypted`
- `bluetooth.classic().onNumericComparisonRequested(cb)` — `peerAddress`と`value`を持つ`EspBluedroidClassicNumericComparison`
- `bluetooth.classic().confirmNumericComparison(peerAddress, accept)`
- `bluetooth.classic().onSecurityChanged(cb)` — `success`とbackendの`status`コード
- `EspBluedroidSppSession::authenticated` / `encrypted`
- `bluetooth.classic().bondCount()` / `bond(i, out)` / `deleteBond(bond)` / `deleteAllBonds()` — **Classic**のlink key store

## 4つのI/O capability

| `ioCapability` | 方式 | このsketchの役割 |
|---|---|---|
| `None` | Just Works | なし |
| `DisplayOnly` | Passkey Entry（表示） | 値を表示する（[SppPasskey](../SppPasskey/)） |
| `KeyboardOnly` | Passkey Entry（入力） | `providePasskey()`で渡す（[SppPasskey](../SppPasskey/)） |
| `DisplayYesNo` | Numeric Comparison | 比較して`confirmNumericComparison()`で答える |

## メモ

- **`responseTimeoutMilliseconds`以内に答えてください**（既定30秒）。未回答の要求は拒否され、認証は失敗します。
- **要求にはpeer addressが含まれ、回答時にも渡します。** そのため、別のpairingに回答が結び付くことがありません。
- **Classicのlink keyとBLEのbondは別のstoreです。** `classic().deleteAllBonds()`はBLEのbondに触れず、`bluetooth.deleteAllBonds()`はClassicのlink keyに触れません。
- **link keyが保存されていれば、次回は確認UIが出ません。** bond済みの相手との再接続は確認なしでsecureになります。もう一度比較を見たい場合は両側で鍵を削除してください。
- 認証失敗後も相手は再試行できます。このexampleはserverを動かし続けます。

## 期待されるSerial出力

```
Compare 419203 with 20:32:c6:1e:9d:4a, then enter y or n
Classic authentication succeeded: peer=20:32:c6:1e:9d:4a status=0
secure SPP connected: authenticated=1 encrypted=1
```
