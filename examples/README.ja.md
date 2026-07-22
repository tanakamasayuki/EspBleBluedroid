# Examples

> English: [README.md](README.md)

| 分類 | Example | 内容 |
|---|---|---|
| Build | [CompileSmoke](CompileSmoke/README.ja.md) | header、Bluedroid backend guard、version macroのbuild確認 |
| GAP | [Advertise](Gap/Advertise/README.ja.md) | Local Name、Service UUID、Manufacturer DataのLegacy Advertising |
| GAP | [Scan](Gap/Scan/README.ja.md) | active scanと`update()` contextの値型Scan Result |
| GAP | [Connect](Gap/Connect/README.ja.md) | Scan Resultから非同期接続し、安定したconnection IDを取得 |
| GATT | [Read](Gatt/Read/README.ja.md) | 接続後の非同期かつbinary-safeなCharacteristic Read / Write |

公開機能を追加するときは、先に対応するunitまたはpeerテストを追加し、その後に
exampleを追加します。
