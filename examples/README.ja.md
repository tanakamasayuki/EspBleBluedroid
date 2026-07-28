# Examples

> English: [README.md](README.md)

| 分類 | Example | 内容 |
|---|---|---|
| Build | [CompileSmoke](CompileSmoke/README.ja.md) | header、Bluedroid backend guard、version macroのbuild確認 |
| GAP | [Advertise](Gap/Advertise/README.ja.md) | Local Name、Service UUID、Manufacturer DataのLegacy Advertising |
| GAP | [Beacon](Gap/Beacon/README.ja.md) | non-connectableなManufacturer Data beacon |
| GAP | [ScanResponse](Gap/ScanResponse/README.ja.md) | Advertising本体とScan Responseを個別に構成 |
| GAP | [ServiceData](Gap/ServiceData/README.ja.md) | 接続せずService UUID付きのbinary値を放送 |
| GAP | [Scan](Gap/Scan/README.ja.md) | active scanと`update()` contextの値型Scan Result |
| GAP | [Connect](Gap/Connect/README.ja.md) | Scan Resultから非同期接続し、安定したconnection IDを取得 |
| GAP | [Mtu](Gap/Mtu/README.ja.md) | 希望ATT MTUを設定し、交換前後の値とpayload上限を確認 |
| GATT Basics | [Client](Gatt/Basics/Client/README.ja.md) | Database Discovery、Read/Write、Descriptor、Notification購読のcallback chain |
| GATT Device | [BatteryClient](Gatt/Device/BatteryClient/README.ja.md) | 標準Battery LevelのReadとNotification購読 |
| Info | [ConnectionInspector](Info/ConnectionInspector/README.ja.md) | 対話式接続、snapshot、bond、counter診断 |
| Info | [ScanDump](Info/ScanDump/README.ja.md) | Advertisingから抽出した全公開fieldを表示 |
| Security | [JustWorksClient](Security/JustWorksClient/README.ja.md) | Just Works pairing、bond保存、暗号化再接続 |
| Security | [StaticPasskeyClient](Security/StaticPasskeyClient/README.ja.md) | 静的passkeyのMITM pairingと認証状態 |
| Security | [RuntimePasskeyClient](Security/RuntimePasskeyClient/README.ja.md) | 実行時入力passkeyによるKeyboardOnlyのMITM pairing |
| Security | [NumericComparisonClient](Security/NumericComparisonClient/README.ja.md) | DisplayYesNoの比較確認によるMITM pairing |
| Classic | [Inquiry](Classic/Inquiry/README.ja.md) | capability確認とname、Class of Device、RSSIを含むClassic機器探索 |
| Classic | [SppServer](Classic/SppServer/README.ja.md) | binary-safe SPP Server sessionと接続・data・切断callback |
| Classic | [SppClient](Classic/SppClient/README.ja.md) | 共通SPP session APIによる非同期SDP/RFCOMM接続 |
| Classic | [SppSerialServer](Classic/SppSerialServer/README.ja.md) | active SPP Server sessionへ自動追従するSerial形式bridge |
| Classic | [SppSerialClient](Classic/SppSerialClient/README.ja.md) | active SPP Client sessionへ自動追従するSerial形式bridge |
| Classic | [SppSecurity](Classic/SppSecurity/README.ja.md) | SSP Numeric Comparisonによる認証・暗号化SPP |
| Classic | [SppPasskey](Classic/SppPasskey/README.ja.md) | DisplayOnly/KeyboardOnly Passkey Entryによるsecure SPP |
| Dual mode | [ScanWhileSpp](DualMode/ScanWhileSpp/README.ja.md) | Classic SPP sessionを維持したactive BLE Scan |

公開機能を追加するときは、先に対応するunitまたはpeerテストを追加し、その後に
exampleを追加します。
