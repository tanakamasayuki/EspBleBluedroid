# NumericComparisonClient

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 3章「セキュリティ編」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

**Numeric Comparison** PairingのCentral側です。対になるのは`DisplayYesNo`の相手です。スマートフォン、EspBleのボードで動かす`Security/NumericComparisonServer`、raw ESP-IDFのpeerなどが該当します（こちらにServerのexampleは無く、理由は[Security/README.ja.md](../README.ja.md)にあります）。設定は**両側でまったく同じ**（`DisplayYesNo` ＋ MITM要求）です。両側が同じ申告をすることが、この方式が選ばれる条件そのものです。

## 必要なもの

- 1 × 無印ESP32（このsketch。Central）
- `DisplayYesNo`の相手 × 1 — スマートフォン、EspBleのボードで動かす`Security/NumericComparisonServer`、またはraw ESP-IDFのpeer

両方のSerialモニタを同時に見られるようにしてください。

## 動作

- ServerのService UUIDをactive scanし、最初の一致へ接続します
- `pairOnConnect`（既定で有効）により、接続と同時にPairingが始まります
- 比較すべき6桁が `onNumericComparison` に届きます。Server側の表示と同じ値のはずです
- `y` で承認、`n` で拒否します。**答えるまでPairingは止まっています**
- 両側が承認したら、`authenticatedRead` を要求するCharacteristicをDiscovery→Readします
- `c` で全Bondを削除します（切断中のみ）

## 主なAPI

- `EspBleSecurityConfig::ioCapability = DisplayYesNo`
- `bluetooth.onNumericComparison(cb)` / `bluetooth.confirmNumericComparison(accept)`
- `bluetooth.discoverCharacteristic(...)` / `bluetooth.readCharacteristic(...)` — Pairing後のアクセス

## 注意

- **片側だけ拒否してもPairingは失敗します。** どちらか一方の `n` で全体が終わります。
- **答えは30秒以内に返してください。** 超えるとスタックが待機を打ち切ります。
- 2回目以降はBondが効いてPairing自体が起きないため、確認を求められません。試し直すには両側で `c` を送ります。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| 比較する側（このsketch） | `onNumericComparison()` + `confirmNumericComparison()` | 同じ |
| 応答の待機時間 | backend依存 | Bluedroidは30秒待ち、その後は認証失敗 |
| 拒否したあと | linkの扱いはbackend次第 | **暗号化されていないBLE linkが残る**。閉じたい場合は`disconnect()`を呼ぶ |
| このライブラリでのPeripheral側 | `Security/NumericComparisonServer` | **対応exampleなし**（[Security/README.ja.md](../README.ja.md)） |

**なぜ違うのか:** Peripheral側は6桁を受け取って答える必要がありますが、着信linkの
connection snapshotが無い状態では、EspBleBluedroidはその要求を配送しません
（[docs/STATUS.ja.md](../../../docs/STATUS.ja.md)参照）。またBluedroidはアプリが
`false`で答えてもlinkを切断しないため、閉じるかどうかはアプリの判断になります。

**移植のしかた:** この側は変更不要です。拒否後に接続を残したくない場合は、明示的な
`disconnect()`を追加してください。

## 期待されるSerial出力

```
Send 'c' while disconnected to clear all bonds.
Does the peer show 052913? Send 'y' to accept, 'n' to reject.
Answer accept: sent
Security established: encrypted=1 authenticated=1 bonded=1
Protected value: MITM protected value
```
