# Security examples

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../docs/GUIDE_BLE_BASICS.ja.md) 3章「セキュリティ編」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../DIFFERENCES_FROM_ESPBLE.ja.md)

Pairing、Bonding、Attributeの保護を扱います。ここのexampleはすべて対の片側です。
`…Client`を1台で動かし、もう1台で`…Server`（またはスマートフォン）を動かします。

| Example | 役割 | 方式 |
|---|---|---|
| [JustWorksServer](JustWorksServer/) | Peripheral | Just Works + Bonding、暗号化Characteristic |
| [JustWorksClient](JustWorksClient/) | Central | Just Works + Bonding、bond storeの確認 |
| [StaticPasskeyServer](StaticPasskeyServer/) | Peripheral | MITM、`DisplayOnly`、firmware固定のpasskey |
| [StaticPasskeyClient](StaticPasskeyClient/) | Central | MITM、`KeyboardOnly`、同じ固定passkey |
| [RuntimePasskeyClient](RuntimePasskeyClient/) | Central | MITM、`KeyboardOnly`、実行時に入力するpasskey |
| [NumericComparisonClient](NumericComparisonClient/) | Central | MITM、`DisplayYesNo`、6桁の一致確認 |

## どちらの側で動かすか

このライブラリでBLE securityを完全に観測できるのは**Central**側です。
`onSecurityChanged()`、`onPasskeyDisplayed()`、`onNumericComparison()`、
`providePasskey()`、`confirmNumericComparison()`はいずれも、この機器が`connect()`で
開いたlinkに対して機能します。

**Peripheral単体**の機器では、EspBleBluedroidがまだconnection snapshotを公開して
いないため、これらのeventは配送されません（[docs/STATUS.ja.md](../../docs/STATUS.ja.md)
参照）。Pairing自体は完了し、その結果（linkの暗号化、bond store、保護attributeへの
アクセス可否）は観測できます。2つのServer exampleが示しているのはこの部分です。

そのため、example一覧はEspBleと非対称になっています。

| EspBleのexample | こちら | 理由 |
|---|---|---|
| `Security/JustWorksServer` | [JustWorksServer](JustWorksServer/) | 移植済み。`onSecurityChanged()`の代わりにbond storeと暗号化Writeで観測する |
| `Security/StaticPasskeyServer` | [StaticPasskeyServer](StaticPasskeyServer/) | 移植済み。passkeyがコンパイル時定数なので、stackから受け取る必要がない |
| `Security/RuntimePasskeyServer` | **対応exampleなし** | 表示側はPairingごとにstackが生成したpasskeyを`onPasskeyDisplayed()`で知る必要がある。そのeventが無い状態では値を表示できないため、この側で動作するsketchはまだ書けない |
| `Security/NumericComparisonServer` | **対応exampleなし** | Peripheralが6桁を受け取り`confirmNumericComparison()`で答える必要がある。snapshotが無いと要求が配送されず認証が拒否されるため、sketchとして成立しない |
| — | [JustWorksClient](JustWorksClient/) | こちらで追加。EspBleがServer側から扱っているJust WorksのCentral半分 |

足りない2つの側を試すには、ここのClient exampleをスマートフォン、EspBleのボード、
またはraw ESP-IDFのpeerと組み合わせてください。ライブラリ自身のpeer test
（`tests/peer/runtime_passkey`、`tests/peer/numeric_comparison`）もその形で検証して
います。

## 検証状況

**Client**側のexampleは、ライブラリ自身の2台peer test（`tests/peer/security_bond`、
`security_passkey`、`runtime_passkey`、`numeric_comparison`）に対応します。これらは
公開APIをCentralとして動かし、raw ESP-IDFのpeerと組み合わせて確認しています。一方、
2つの**Server** exampleはまだpeer testの対象ではありません。attributeのpermissionと
bond storeはテスト済みのライブラリ挙動ですが、この2つのsketchにおけるPeripheral側の
pairingの流れは自動化された実機テストで固定していません。APIの使用例として扱い、
検証済みの挙動としては扱わないでください。

## 共通の設定

```cpp
EspBleConfig config;
config.security.enabled = true;          // securityを有効にする
config.security.bonding = true;          // 鍵を保存して再接続時に自動暗号化する
config.security.pairOnConnect = true;    // linkが成立したらPairingを開始する
config.security.mitm = true;             // MITM保護（passkey / 比較確認）を要求する
config.security.ioCapability = EspBleSecurityIoCapability::KeyboardOnly;
config.security.staticPasskeyEnabled = true;
config.security.staticPasskey = 438209;
```

| I/O capability | 選ばれる方式 | sketchの役割 |
|---|---|---|
| `None` | Just Works | なし |
| `DisplayOnly` | Passkey Entry（表示側） | passkeyを表示する |
| `KeyboardOnly` | Passkey Entry（入力側） | `providePasskey()`で渡す |
| `DisplayYesNo` | Numeric Comparison | 比較して`confirmNumericComparison(accept)`で答える |

## すべてに共通する注意

- **Bluedroidの待機上限は30秒**です（`providePasskey()`／`confirmNumericComparison()`）。
  未回答の要求は認証失敗になります。
- **Numeric Comparisonを拒否してもlinkは切れません。** Bluedroidは暗号化されていない
  BLE接続をそのまま維持するため、閉じたい場合はアプリから`disconnect()`します。
- **passkey待機中の`disconnect()`と`end()`は待機をcancelして即時終了します。**
- **Bondの削除は非同期です。** `deleteBond()`／`deleteAllBonds()`は切断中に呼んでください。
  Bluedroidの永続ストアが落ち着くまで待ってから戻ります。
- **Bondはトランスポートごとに別管理です。** BLEのbondは`bluetooth.bondCount()`／
  `bond()`／`deleteBond()`、Bluetooth Classicのbondは`bluetooth.classic().bondCount()`
  などにあります（[Classic/SppSecurity](../Classic/SppSecurity/)）。
- **同一boot内でpasskeyの構成を変える場合は再起動が必要になることがあります。**
  Arduino-ESP32のBLE wrapperがprocess内のpasskey設定を解除できないためです。
