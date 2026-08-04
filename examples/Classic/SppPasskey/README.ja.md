# SppPasskey

> English: [README.md](README.md)
> 概念の説明: [Bluetooth Classic通信の入門ガイド](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) Securityの章
> EspBle: 対応exampleなし — Bluetooth Classic専用（[DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)）

Classic SSPの**Passkey Entry**を使う、認証・暗号化ありのSPP serverです。既定は`KeyboardOnly`で、相手が表示した6桁をSerial Monitorへ入力します。

[SppSecurity](../SppSecurity/)は両側が同じ数値を比較しますが、Passkey Entryは一方が**表示**し、他方が**入力**します。このsketchは両方向に対応します。`ioCapability`を`DisplayOnly`にすれば、ESP32側が表示する側になります。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（SPP server。既定ではpasskey**入力**側）
- passkeyを表示する相手 × 1 — スマートフォン、または`DisplayOnly`に設定した2台目のボード

## 動作

- `begin()`の前に`ioCapability = KeyboardOnly`でClassic securityを有効にします
- `AuthenticatedEncrypted`を要求するSPP serverを開始します
- `onPasskeyRequested()`でpeer addressを記憶し、6桁の入力を求めます
- 入力された値を`providePasskey(peerAddress, passkey)`で渡します
- `onPasskeyDisplayed()`も実装しているので、`DisplayOnly`に設定した場合も同じsketchで動きます
- 認証結果とsessionの`authenticated` / `encrypted`を表示し、データをechoします

## 主なAPI

- `EspBleConfig::classicSecurity.ioCapability` — `KeyboardOnly`（入力）または`DisplayOnly`（表示）
- `bluetooth.classic().onPasskeyRequested(cb)` — `EspBluedroidClassicPasskeyRequested::peerAddress`
- `bluetooth.classic().onPasskeyDisplayed(cb)` — `peerAddress`と`passkey`を持つ`EspBluedroidClassicPasskeyDisplayed`
- `bluetooth.classic().providePasskey(peerAddress, passkey)` — 保留中の要求へ回答します。`true`は回答が受理されたことを意味し、pairingの成功を意味しません
- `bluetooth.classic().onSecurityChanged(cb)` — 実際のpairing結果
- `bluetooth.classic().bondCount()` / `deleteAllBonds()` — Classicのlink key store

## メモ

- **`providePasskey()`のtrueは「回答が受理された」だけを意味します。** pairingが成功したかは`onSecurityChanged()`で確認します。
- **期限後の入力は拒否されます。** 要求は`responseTimeoutMilliseconds`（既定30秒）で失効し、その後の入力は失敗します。相手はそのまま再試行できます。
- **passkey待機中の`disconnect()`と`end()`は待機をcancelして速やかに戻ります。** timeout満了までブロックしません。
- **同一boot内でI/O capabilityを変えると再起動が必要になることがあります。** Bluedroidはpairingの構成をprocess全体で保持するため、表示側の構成で`end()`したあとに`KeyboardOnly`へ変える場合は再起動してください。
- passkeyは相手が提示するpairingごとの値です。ここに固定値を書かないでください。静的passkeyのパターンはBLE側のものです（[Security/StaticPasskeyServer](../../Security/StaticPasskeyServer/)）。

## 期待されるSerial出力

```
Enter the six-digit passkey shown by 20:32:c6:1e:9d:4a:
Passkey reply accepted: 1
Security succeeded for 20:32:c6:1e:9d:4a (status=0)
Secure SPP connected: authenticated=1 encrypted=1
```
