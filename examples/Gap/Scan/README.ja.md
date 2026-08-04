# Scan

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 2章「GAP編 — 探してつながる」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

継続的なactive scanを実行し、受信したadvertisementのaddress、RSSI、（存在すれば）デバイス名を表示します。Central側の**最小例**です。2台目のボードで[Advertise](../Advertise/) exampleを動かして組み合わせるか、周囲のBLE機器の観察に使えます。

ここで表示するのは3項目だけです。Service UUID・Service Data・Manufacturer Data・iBeaconまで含めて**全フィールドを見たい場合は[Info/ScanDump](../../Info/ScanDump/)**を使ってください。このexampleは「スキャンを始めて結果を受け取る」最小の書き方を示すことに絞っています。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（Central）
- 任意の相手 — 2台目のボードで[Advertise](../Advertise/) example、または周囲の任意のBLE機器

## 動作

- 時間無制限（`durationSeconds = 0`）のactive scanを開始します
- 各Scan Resultは値型としてcopyされ、`bluetooth.update()`のcontextでcallbackへ配送されます（BLE stack task上では実行されません）
- 全resultのaddress、RSSI、name（存在時）を表示します

## 主なAPI

- `bluetooth.scanner().onResult(callback)` — advertisementごとに`EspBleScanResult`を受け取ります
  - `scanResult.address`、`scanResult.rssi`、`scanResult.hasName()`、`scanResult.name`
  - ほかに`advertisesService(uuid)`、`connectable`、Manufacturer Dataも参照できます
- `EspBleScanConfig` — `active`、`wantDuplicates`、`intervalMilliseconds`、`windowMilliseconds`、`durationSeconds`、`acceptListOnly`
  - `acceptListOnly = true` にすると、`bluetooth.addToAcceptList()` で登録した相手のアドバタイズだけを受け取ります。それ以外はコントローラが捨てるので`onResult`まで届きません（[Gap/AcceptList](../AcceptList/)は同じリストを接続の制限に使う例です）。照合はアドレス単位なので、RPAを回転させる相手はbonding後でないと登録できません
- `bluetooth.scanner().start(scanConfig)` / `bluetooth.scanner().stop()`
- `bluetooth.scanner().droppedResultCount()` — queue溢れで取りこぼしたresult数

## 注意

- **AdvertisingとScan Responseはaddress単位でmergeされます。** Bluedroidは、Scan Responseが届く前にAdvertising payloadだけを先に報告することがあります。ライブラリは短時間そのaddressの結果を保持し、Scan Response側のフィールドをmergeしてから、1件の`EspBleScanResult`としてcallbackへ配送します。名前がScan Response側にある相手でも、Active Scanで名前が取れるのはこのためです。
- **結果queueは16件です。** 結果はBLE stack taskで受け取ってqueueへコピーし、`update()`から配送します。`update()`を呼ばなくなった場合やcallbackの中で待ってしまった場合、queueが溢れた分は`droppedResultCount()`で数えられます。
- `end()`はqueueに残った未配送の結果を配送せずに破棄します。
- Classicの機器探索は**別の操作**で、結果型も別です（[Classic/Inquiry](../../Classic/Inquiry/)）。BLE Scanとの同時実行は保証していません。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| `onResult()`の配送 | 値コピー、`update()`から | 同じ |
| 結果queue | 上限あり、`droppedResultCount()` | 16件、`droppedResultCount()` |
| AdvertisingとScan Responseのmerge | backendが行う | ライブラリがaddress単位で短時間だけ行う |
| 対になる操作 | — | Bluetooth ClassicのInquiry（[Classic/Inquiry](../../Classic/Inquiry/)） |

**なぜ違うのか:** BluedroidはPDUごとに1つのGAP result eventを上げるため、Active Scanでは同じ相手のAdvertising payloadがScan Responseより先に出ることがあります。両方をそのまま配送すると同じ機器が別のフィールドで2回現れてしまうので、ライブラリがaddressでmergeしてから1件だけアプリへ渡します。

**移植のしかた:** コード変更は不要です。

## 期待されるSerial出力

```
5a:b8:1e:0c:2f:71 RSSI=-52 name=EspBleBluedroid Advertiser
70:04:1d:32:99:a0 RSSI=-78
...
```
