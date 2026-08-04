# PrivateAddress

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 2章「GAP編 — 探してつながる」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

`EspBleConfig::ownAddressType`で選択し、工場出荷のpublic addressの代わりにprivate addressでadvertiseします。connectableなPeripheralの例です。2台目のボードで組み合わせる[Scan](../Scan/) exampleでaddress typeを確認できます。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（Peripheral）
- BLEスキャナ — 2台目のボードで[Scan](../Scan/) example、またはnRF Connect等のスキャナアプリ

## 2つのモード

sketch冒頭の `USE_RESOLVABLE_PRIVATE_ADDRESS` で切り替えます。

| | RandomStatic（既定） | ResolvablePrivate（RPA） |
|---|---|---|
| アドレス | `begin()`で生成する固定random | controllerが周期的に変える |
| 追跡耐性 | public addressは隠せるが、この固定値で追跡はできる | 回転するので追跡されにくい |
| bonding | 不要 | **必須**。peerはIRKでアドレスを解決する |
| 単体で動くか | 動く | securityなしでは相手が再接続できない |

RPAのprivacyはcontrollerが管理します。回転周期はBluedroid controllerが決め、アプリからは変更できません。

## 動作

- 選んだモードに応じて `config.ownAddressType` を設定します。RPAモードでは `config.security.enabled` / `bonding` も併せて有効にします
- connectableなPeripheralとしてadvertiseし、スキャナがaddress typeを観測できるようにします
- 実際に電波へ乗せたアドレスとそのaddress typeを表示します

## 主なAPI

- `EspBleConfig::ownAddressType` — `Public`（既定） / `RandomStatic` / `ResolvablePrivate`
- `EspBleConfig::security.enabled` / `bonding` — RPAを使う場合に必要
- `bluetooth.localAddress()` / `localAddressType()` — 電波に乗っているアドレス。**RPAでは空文字列**になります（後述の違いを参照）

## メモ

- `Public` — 工場出荷のpublic address。`RandomStatic` — `begin()`で生成する固定random static address（回転しない固定identity）。`ResolvablePrivate` — controllerが自身のタイマで回転させるRPA。bonded peerがIRKで解決するためsecurity/bonding併用時のみ有用で、未bondのスキャナには変化するrandom addressにしか見えません。
- スキャナからはaddress typeが**Random**（Publicではない）に見えます。static random addressは先頭octetの上位2bitが`0b11`です。
- Extended / Periodic Advertisingは利用できません。無印ESP32のcontrollerはLegacy Advertisingのみに対応します。
- Scan RequestはPublic addressのまま送出されます。private addressはAdvertisingのidentityに適用されます。
- accept listはアドレスで照合するため、RPAを使う相手はbondingしてidentity addressが効くようになるまで登録できません（[AcceptList](../AcceptList/)参照）。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| RPA時の`localAddress()` | 現在のRPAを返す | 空の`String`を返す |
| RPAの回転周期 | backendのビルド設定で固定 | controllerが決定 |
| Peripheral側の接続／切断callback | 配送される | **配送されない** — このsketchは代わりに公開アドレスを表示する |
| Extended / Periodic Advertising | 利用不可 | 利用不可（controller非対応） |

**なぜ違うのか:** 無印ESP32のcontrollerはRPAを内部で生成し、対応するGAP APIには現在値を読み戻す呼び出しがありません。古い値や作り出した値を返さないために、ライブラリは空の`String`を返します。また、EspBleBluedroidの`onConnected()`／`onDisconnected()`はこの機器が`connect()`で開いたlinkを表すもので、着信するPeripheral linkはまだconnection snapshotとして公開していません（[docs/STATUS.ja.md](../../../docs/STATUS.ja.md)参照）。

**移植のしかた:** 空の`localAddress()`をエラーではなく「controller管理」として扱い、接続の確認はPeripheral側のcallbackではなくCentral側（[Gap/Scan](../Scan/)、[Info/ScanDump](../../Info/ScanDump/)）で行います。

## 期待されるSerial出力

```
Advertising with a random static address; current=c7:41:9b:2e:55:8a type=1
```
