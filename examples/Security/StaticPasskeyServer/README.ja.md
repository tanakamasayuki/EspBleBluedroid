# StaticPasskeyServer

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 3章「セキュリティ編」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

静的6桁passkeyによるMITM認証Pairingを要求するGATT Server（Peripheral）です。このボードは表示側（`DisplayOnly`）で、passkeyをSerialへ表示し、接続するCentralがそれを入力します。[StaticPasskeyClient](../StaticPasskeyClient/) example（passkey入力側）と接続します。

## 必要なもの

- 1 × 無印ESP32（このsketch。Peripheral / GATT Server、passkey表示側）
- 1 × キー入力できるBLE Central: [StaticPasskeyClient](../StaticPasskeyClient/) exampleまたはスマートフォンアプリ

## 動作

- `begin()`前に`authenticatedRead` / `authenticatedWrite` permissionつきのCharacteristicを登録します
- `mitm`、`ioCapability = DisplayOnly`、静的passkey `438209`でsecurityを有効化します
- 起動時にpasskeyを表示します。コンパイル時定数なので、値を知るためのcallbackは要りません
- bond数が変化したら保存されたbondを表示します。これがMITM認証Pairingの完了を示すPeripheral側の証拠です

## 主なAPI

- `EspBleGattCharacteristicConfig::authenticatedRead` / `authenticatedWrite` — MITM認証済みlinkを要求します
- `EspBleSecurityConfig` — `mitm`、`ioCapability`（`DisplayOnly` / `KeyboardOnly`）、`staticPasskeyEnabled`、`staticPasskey`
- `bluetooth.bondCount()` / `bluetooth.bond(index, out)` — 機器単位のbond store。connection snapshotなしで参照できます

## メモ

- CharacteristicはMITM認証済みlinkを要求するため、Just Works Pairingではアクセスできません。
- passkeyはexample用の固定値です。製品では共通のhard-coded値ではなく、デバイスごとに安全にprovisioningしてください。
- **この側で成立するのは静的方式です。** 実行時生成のpasskeyはstackから通知してもらう必要があり、Peripheral単体の機器にはそのeventが届きません。`RuntimePasskeyServer`に対応するexampleが無いのはこのためです（[Security/README.ja.md](../README.ja.md)参照）。
- **同一boot内でpasskeyの構成を変える場合は再起動が必要です。** Arduino-ESP32のBLE wrapperはprocess内のpasskey設定を解除できないため、静的／`DisplayOnly`のpasskeyで`end()`したあとに`KeyboardOnly`の実行時入力へ切り替えるには再起動してください。同じ構成での再初期化には影響しません。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| Peripheral単体機器での`onPasskeyDisplayed()` | Pairing開始時に配送される | **配送されない** — 静的な定数をそのまま表示する |
| Peripheral単体機器での`onSecurityChanged()` | 配送される | **配送されない** — `bondCount()`を監視する |
| advertising再開のための`onDisconnected()` | 配送される | **配送されない** |
| `authenticatedRead`／`authenticatedWrite` | ATTが強制 | 同じ |
| `end()`後のpasskey I/O capability変更 | 可能 | `KeyboardOnly`の実行時入力へ移る場合は再起動が必要 |

**なぜ違うのか:** [JustWorksServer](../JustWorksServer/)と同じ理由です。BLEのsecurity eventはconnection snapshotに結び付くもので、EspBleBluedroidはこの機器が`connect()`で開いたlinkのsnapshotだけを公開します（[docs/STATUS.ja.md](../../../docs/STATUS.ja.md)参照）。静的passkeyであれば失われる情報はありません。ユーザーが入力すべき値はsketch自身がすでに知っているからです。

**移植のしかた:** passkeyの表示を`onPasskeyDisplayed()`から`setup()`へ移し、`onSecurityChanged()`を`bondCount()`の監視へ置き換えます。Characteristicのpermissionと`EspBleSecurityConfig`のフィールドの使い方はEspBleと同じです。

## 期待されるSerial出力

```
Enter passkey 438209 on the peer.
Advertising. Stored bonds: 0
Bonded with 5a:b8:1e:0c:2f:71 (total 1)
```
