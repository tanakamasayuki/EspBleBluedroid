# JustWorksServer

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 3章「セキュリティ編」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

暗号化されたlinkを要求するCharacteristicを持つGATT Server（Peripheral）です。PairingはJust Works（passkeyなし、LE Secure Connections）+ Bondingで、接続時に自動開始します。任意のBLE Central（nRF Connectなどのスマートフォンアプリ、または別ボード）と接続できます。

## 必要なもの

- 1 × 無印ESP32（このsketch。Peripheral / GATT Server）
- 1 × Pairing可能なBLE Central（スマートフォンアプリまたは2台目のボード）

## 動作

- `begin()`前に`encryptedRead` / `encryptedWrite` permissionつきのCharacteristicを登録します
- Bondingと`pairOnConnect`つきでsecurityを有効化し、Central接続と同時にPairingを開始します
- 暗号化Characteristicへ書き込まれた値を表示します。Writeが成功したこと**そのもの**が、ATT層が暗号化linkを受理した証拠になります
- bond数が変化したら、保存されたbondを表示します
- Serialコマンド`c`で全Bondを削除し（切断中のみ許可）、残数を表示します

## 主なAPI

- `EspBleGattCharacteristicConfig::encryptedRead` / `encryptedWrite` — ATT層で暗号化linkを強制します
- `EspBleSecurityConfig` — `enabled`、`bonding`、`pairOnConnect`
- `bluetooth.bondCount()` / `bluetooth.bond(index, out)` — 機器単位のbond store。connection snapshotなしで参照できます
- `bluetooth.deleteAllBonds()` — Bond storeの管理
- `gattServer.onWritten(callback)` — 暗号化linkでなければ実行できないWrite

## メモ

- Just Works Pairingは`encrypted=1`ですが`authenticated=0`（MITM非認証）です。暗号化前にCharacteristicを読むとinsufficient-encryptionエラーになり、OS側のPairingが誘発されます。
- **Attributeのpermissionはこのsketchではなく、ATT層が強制します。** この部分はPairingをどちら側で観測するかに関係なく機能します。
- Bondの削除はBluedroidの非同期な永続ストア更新を待ってから戻るため、切断中に呼んでください。
- Central側から同じPairingを見るには[Security/JustWorksClient](../JustWorksClient/)と組み合わせてください。そちらではsecurity eventが配送されます。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| Peripheral単体機器での`onSecurityChanged()` | 配送される | **配送されない** — Writeとbond storeで観測する |
| Peripheralでの`onPasskeyDisplayed()`／`onNumericComparison()` | 配送される | **配送されない** |
| advertising再開のための`onDisconnected()` | 配送される | **配送されない** — コマンドやタイマから再開する |
| 暗号化／認証必須のattribute permission | ATTが強制 | 同じ |
| `bondCount()`／`bond()`／`deleteAllBonds()` | 機器単位 | 同じ |
| `Security/RuntimePasskeyServer`、`Security/NumericComparisonServer` | あり | **対応exampleなし**（[Security/README.ja.md](../README.ja.md)参照） |

**なぜ違うのか:** BLEのsecurity eventはbackendがactiveなconnection snapshotに対して上げるもので、EspBleBluedroidが公開するsnapshotはこの機器が`connect()`で開いたlinkに限られます（[docs/STATUS.ja.md](../../../docs/STATUS.ja.md)参照）。純粋なPeripheralとして動く機器にはeventを結び付けるsnapshotが無いため、配送されません。Pairing自体は完了し、その結果（linkの暗号化、bond storeへの登録、保護attributeへのアクセス可否）はすべて観測できます。

**移植のしかた:** `onSecurityChanged()`のハンドラを外し、結果のほうを読みます。`bondCount()`を監視し、保護されたCharacteristicのRead/Write成功を暗号化linkの証拠として扱います。eventのトレースが欲しい場合は、Central側をスマートフォン、[Security/JustWorksClient](../JustWorksClient/)、またはEspBleのボードで動かしてください。

## 期待されるSerial出力

```
Advertising. Stored bonds: 0
Send 'c' while disconnected to clear all bonds.
Bonded with 5a:b8:1e:0c:2f:71 (total 1)
Encrypted write: hello
```
