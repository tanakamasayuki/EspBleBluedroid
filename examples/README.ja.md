# Examples

> English: [README.md](README.md)

| 分類 | Example | 内容 |
|---|---|---|
| Build | [CompileSmoke](CompileSmoke/README.ja.md) | header、Bluedroid backend guard、version macroのbuild確認 |
| GAP | [Advertise](Gap/Advertise/README.ja.md) | Local Name、Service UUID、Manufacturer DataのLegacy Advertising |
| GAP | [Scan](Gap/Scan/README.ja.md) | active scanと`update()` contextの値型Scan Result |
| GAP | [Connect](Gap/Connect/README.ja.md) | Scan Resultから非同期接続し、安定したconnection IDを取得 |
| GATT | [Read](Gatt/Read/README.ja.md) | 接続後の非同期Battery Characteristic Read |
| GATT | [Client](Gatt/Client/README.ja.md) | Read、Write、Notification購読のcallback chain |
| Security | [JustWorksClient](Security/JustWorksClient/README.ja.md) | Just Works pairing、bond保存、暗号化再接続 |
| Security | [StaticPasskeyClient](Security/StaticPasskeyClient/README.ja.md) | 静的passkeyのMITM pairingと認証状態 |
| Security | [RuntimePasskeyClient](Security/RuntimePasskeyClient/README.ja.md) | 実行時入力passkeyによるKeyboardOnlyのMITM pairing |
| Security | [NumericComparisonClient](Security/NumericComparisonClient/README.ja.md) | DisplayYesNoの比較確認によるMITM pairing |
| Classic | [Inquiry](Classic/Inquiry/README.ja.md) | capability確認とname、Class of Device、RSSIを含むClassic機器探索 |
| Classic | [SppServer](Classic/SppServer/README.ja.md) | binary-safe SPP Server sessionと接続・data・切断callback |
| Classic | [SppClient](Classic/SppClient/README.ja.md) | 共通SPP session APIによる非同期SDP/RFCOMM接続 |
| Dual mode | [ScanWhileSpp](DualMode/ScanWhileSpp/README.ja.md) | Classic SPP sessionを維持したactive BLE Scan |

公開機能を追加するときは、先に対応するunitまたはpeerテストを追加し、その後に
exampleを追加します。
