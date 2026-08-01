# AcceptList

> English: [README.md](README.md)

接続してくる相手を制限するPeripheral側の例です。

BLEには、接続要求をapplicationが見てから承認・拒否するcallbackはありません。Filter Accept List（旧称white list）を使うと、controllerが接続要求をアドレスと照合し、登録されていない相手をapplicationへ届く前に破棄します。

同じlistはCentral側のScanにも使われ、`EspBleScanConfig::acceptListOnly = true`で
一覧外のAdvertisingをcontrollerが破棄します。

## 必要なもの

- このsketchを動かす無印ESP32 × 1
- 接続を試みるCentral（2台目のESP32、またはスマートフォン）

`ALLOWED_CENTRAL`を、接続を許可するCentralのidentity addressへ書き換えてください。アドレス種別も`Public`または`Random`へ合わせます。初期値のままなら通常は誰も接続できません。

## 動作

- 許可アドレスをaccept listへ追加します
- `ConnectionFromAcceptList`でadvertisingし、一覧外からの接続要求をcontrollerで破棄します
- Serialへ`o`を送ると`Any`へ切り替わり、誰でも接続できます
- `r`を送ると再びaccept listによる制限へ戻ります

現在の公開APIはPeripheral側のGATT Serverと接続callbackをまだ扱わないため、このexampleではCentral側の接続成否で動作を確認します。

## 主なAPI

- `bluetooth.addToAcceptList(address, addressType)` — 最大8件。重複追加は成功し、件数は増えません
- `bluetooth.removeFromAcceptList(address, addressType)`
- `bluetooth.clearAcceptList()`
- `bluetooth.acceptListCount()` / `bluetooth.acceptListEntry(index, entry)`
- `bluetooth.advertising().setFilterPolicy(policy)`
- `EspBleScanConfig::acceptListOnly` — 同じlistをScan側へ適用

policyは次の4種類です。

| policy | Scan Request | 接続要求 |
|---|---|---|
| `Any` | 全員を許可 | 全員を許可 |
| `ScanRequestFromAcceptList` | 一覧内だけ許可 | 全員を許可 |
| `ConnectionFromAcceptList` | 全員を許可 | 一覧内だけ許可 |
| `Both` | 一覧内だけ許可 | 一覧内だけ許可 |

## 注意

- policyとaccept listは次に`advertising.start()`した時点でcontrollerへ反映されます。動作中に変える場合は`stop()`、変更、`start()`の順にします。
- 制限policyで一覧が空なら、該当する要求はすべて破棄されます。
- 拒否されたCentralには拒否通知が返らず、接続失敗またはtimeoutに見えます。
- RPAを使うpeerは、bonding後のidentity addressと正しいaddress typeを登録します。観測した一時的なRPAを固定登録してはいけません。
- 接続制限は属性値の暗号化を代替しません。値を守る場合はBLE Securityも設定します。

## 期待されるSerial出力

```text
Restricted advertising. Only aa:bb:cc:dd:ee:ff may connect. Send o/r to change policy.
Policy: open (accept list has 1 entries)
Policy: restricted (accept list has 1 entries)
```
