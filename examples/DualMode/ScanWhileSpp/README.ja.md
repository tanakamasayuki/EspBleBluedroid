# ScanWhileSpp

> English: [README.md](README.md)
> 概念の説明: [Bluetooth Classic通信の入門ガイド](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) dual modeの章
> EspBle: 対応exampleなし — Bluetooth Classicが必要（[DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)）

**BLEとBluetooth Classicを同時に**動かす例です。SPP sessionを接続したまま、Active BLE Scanを実行します。

1つの`EspBleBluedroid`オブジェクトが1つのdual mode Bluedroid stackを所有しますが、2つのトランスポートはAPIも結果型も分かれたままです。BLEはroot直下（`scanner()`、`connect()`、`gattServer()`）、Classicは`classic()`配下にあります。統合しないのは意図的です。BLEのscan resultとClassicのinquiry resultは同じものではありません。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（SPP client **かつ** BLE Central）
- SPP server × 1 — 2台目のボードで[Classic/SppServer](../../Classic/SppServer/)、またはスマートフォンのシリアルターミナル
- 任意: 近くにBLE advertiser（[Gap/Advertise](../../Gap/Advertise/)など）。scanが報告する対象になります

## 動作

- Classic addressの入力を促し、`classic().spp().connect()`を呼びます
- `onConnected()`でSPPのsession IDを記憶し、**10秒間のActive BLE Scan**を開始します
- 各BLE scan結果を、activeなままのSPP session IDと一緒に表示します。これが両トランスポートが生きている証拠になります
- SPP sessionが切れたらscanを停止します
- SPPの接続失敗を詳細文字列付きで表示します

## 主なAPI

- `bluetooth.classic().spp()` — `connect()`、`onConnected()`、`onDisconnected()`、`onConnectionFailed()`、`write()`
- `bluetooth.scanner()` — `start(config)`、`stop()`、`isScanning()`、`onResult()`
- `bluetooth.update()` — **両方**のevent経路を回す唯一のpump
- `bluetooth.capabilities()` — `dualMode`でそのbuildが対応しているかを確認できます

## 実機で確認している範囲

このexampleの背後にある2台構成のpeer testは、sketchよりさらに踏み込んでいます。

- SPP sessionがbinary dataを運んでいる状態での、Active BLE Scan、BLE Central接続、GATT Discovery、Read / Write、Notification
- 同じ接続・購読上での64→128→256件のNotification burstを、roundごとに集計。配送されたNotification数、`droppedEventCount()`の増分、SPPの往復byte数、受信ringのpacket数がすべて一致します
- BLE event queueが飽和した場合は**制御eventが優先されます**。接続・Security・GATT完了などは保持し、最古のNotificationをdropするため、飽和状態でも完了eventが失われません

長時間soakと、連続飽和状態でのfairnessは未確認です。

## メモ

- **`update()`を呼び続けてください。** BLEとClassicのcallbackはどちらもここから配送されます。`loop()`をブロックすると両方が止まります。
- **Classic InquiryとBLE Scanは別の操作です。** 同時実行は保証していません。このexampleが同時に動かしているのはSPPの*session*とBLE Scanで、InquiryとScanではありません。
- **BLEのevent queueは固定長です。** Notification burst時は`bluetooth.droppedEventCount()`によって、欠落を暗黙にではなく明示的に観測できます。
- SPP sessionもBLE Central接続も、同時に1つだけです。

## 期待されるSerial出力

```
Enter the Classic address of an SPP Server
BLE 5a:b8:1e:0c:2f:71 RSSI=-52 while SPP session 1 is active
BLE 70:04:1d:32:99:a0 RSSI=-78 while SPP session 1 is active
```
