# StaticPasskeyClient

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 3章「セキュリティ編」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

[StaticPasskeyServer](../StaticPasskeyServer/)のCentral側、MITM認証Pairingでpasskeyを「入力する」側（`KeyboardOnly`）です。Pairing成功後、認証済みlinkを要求するCharacteristicをDiscovery→Readします。

passkeyをsketchに固定するため、実行時の入力を伴いません。**利用者が実際に打ち込む形**は[RuntimePasskeyClient](../RuntimePasskeyClient/)を参照してください。

## 必要なもの

- 1 × 無印ESP32（このsketch。Central、passkey入力側）
- 1 × 無印ESP32（[StaticPasskeyServer](../StaticPasskeyServer/) exampleを動かす）

## 動作

- ServerのService UUIDをactive scanし、最初の一致へ接続します
- 接続時に`requestSecurity()`で明示的にPairingを開始します
- Security成功時にMITM保護されたCharacteristicをDiscoveryしてReadします
- Securityの結果と保護された値を表示します
- Serialコマンド`c`で全Bondを削除し（切断中のみ許可）、残数を表示します

## 主なAPI

- `EspBleSecurityConfig::ioCapability = KeyboardOnly` — passkeyを「入力する」側
- `config.security.staticPasskeyEnabled` / `staticPasskey` — スタックへ渡す事前設定passkey
- `bluetooth.requestSecurity(connectionId)` — 明示的なPairing開始。完了は`onSecurityChanged()`へ届きます
- `bluetooth.discoverCharacteristic(...)` / `bluetooth.readCharacteristic(...)` — Pairing後にCharacteristicへアクセス

## メモ

- 固定passkeyはスタックへ事前に渡されるため、ここの`STATIC_PASSKEY`はServerが表示する値と一致させる必要があります。**値がsketchに焼き込まれる以上、ソースを読める相手には秘密になりません。** 実運用では実行時入力の形（[RuntimePasskeyClient](../RuntimePasskeyClient/)）を選んでください。
- `authenticatedRead`のCharacteristicはMITM Pairing完了後にのみReadできます。それ以前のReadはATTのsecurityエラーで失敗します。

## EspBleとの違い

この側の使い方はEspBleと同じで、違うのは対になるPeripheral側だけです。Peripheral側の
example構成が非対称な理由は[Security/README.ja.md](../README.ja.md)、ライブラリ全体の
一覧は[DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)にあります。

## 期待されるSerial出力

```
Send 'c' while disconnected to clear all bonds.
Security established: encrypted=1 authenticated=1 bonded=1
Protected value: MITM protected value
```
