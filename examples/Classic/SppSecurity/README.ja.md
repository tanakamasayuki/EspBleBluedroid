# Classic SPP Security

> English: [README.md](README.md)

SSP認証とlink暗号化を必須にするSPP Server exampleです。Classic Securityのpairing
UIは`EspBleConfig`へ1回設定し、profileのSecurity要件は
`EspBluedroidSppServerConfig`へ明示します。将来別のClassic profileを追加しても、
pairing UIを共有しながらSPP固有policyを持ち込まないための分離です。

両端がDisplayYesNoの場合、`EspBluedroidClassicNumericComparison`は
`bluetooth.update()`から配送されます。両端の6桁値を比較し、Serial Monitorへ
`y`または`n`を入力します。未回答requestは`responseTimeoutMilliseconds`後に
拒否されます。

backendの認証結果は`EspBluedroidClassicSecurityChanged`で通知されます。確立した
secure SPP sessionでは`authenticated`と`encrypted`がtrueになります。
保存されたClassic link keyはBLE bondとは分離され、`classic().bondCount()`、
`bond()`、`deleteBond()`、`deleteAllBonds()`で管理できます。
対象はArduino-ESP32 3.3.11の無印ESP32で、PSRAMは不要です。
