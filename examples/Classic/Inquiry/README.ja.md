# Inquiry

> English: [README.md](README.md)
> 概念の説明: [Bluetooth Classic通信の入門ガイド](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) Inquiryの章
> EspBle: 対応exampleなし — Bluetooth Classic専用（[DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)）

周囲の**discoverableなBluetooth Classic機器**を探し、address、name、RSSI、Class of Deviceを表示します。

InquiryはBLE ScanのClassic版に相当しますが、このライブラリは両者を**意図的に分離**しています。`bluetooth.classic().inquiry()`はClassicのフィールドを返し、`bluetooth.scanner()`はBLEのAdvertising dataを返します。別の操作・別の結果型であり、同時実行は保証していません。

## 必要なもの

- このsketchを動かす無印ESP32 × 1
- 近くにあるdiscoverableなClassic機器 — Bluetooth設定画面を開いたスマートフォン、ペアリングモードのClassicヘッドセット、または[SppServer](../SppServer/)を動かす2台目のボード

*ペアリング済みだがdiscoverableでない*機器はInquiryに応答しません。これはBluetooth Classicの性質で、discoverableは相手が意図的に入るモードです。

## 動作

- `begin()`の**前に**`capabilities().classicInquiry`を確認します。Classic非対応のbuildでは、後で失敗するのではなくその旨を表示します
- `begin()`で共有Bluedroid stackを初期化します
- 10秒のInquiryを開始します
- 各結果を表示します。addressに加え、相手が提供した場合はname / RSSI / Class of Deviceも表示します
- 完了eventを、cancelされたかどうかも含めて表示します

## 主なAPI

- `bluetooth.capabilities()` — `EspBluedroidCapabilities::classicInquiry`、`classicSpp`、`classic`、`dualMode`、`ble`
- `bluetooth.classic().inquiry().start(config)` — `EspBluedroidClassicInquiryConfig::durationSeconds`（1〜61）と`maxResponses`（0はbackend上限まで）
- `bluetooth.classic().inquiry().stop()` — 停止を要求します。その後の完了eventで`cancelled = true`になります
- `bluetooth.classic().inquiry().isRunning()` / `droppedResultCount()`
- `onResult(callback)` — `address`、`name`、`rssi` / `hasRssi`、`classOfDevice` / `hasClassOfDevice`を持つ`EspBluedroidClassicInquiryResult`
- `onComplete(callback)` — `EspBluedroidClassicInquiryComplete::cancelled`

## メモ

- **callbackはBluedroid callbackではなく`bluetooth.update()`から配送されます。** 各結果は値型のコピーです。`update()`を呼び続けないと何も届きません。
- **結果queueは16件です。** 溢れた分は`droppedResultCount()`で数えられます。
- **`hasRssi`と`hasClassOfDevice`の確認が必要です。** すべての応答がそれらを含むわけではないので、値を読む前にフラグを見てください。
- **nameは空で届くことがあります。** Bluedroidはfriendly nameを別の手順で解決するため、addressだけの結果が報告されることがあります。
- `stop()`は`onResult()`の中から呼べます。目的の機器が見つかった時点で止めるのが一般的な使い方です。

## 期待されるSerial出力

```
20:32:c6:1e:9d:4a name=Pixel 8 RSSI=-54 CoD=0x5a020c
d0:cf:13:58:fd:95 name=EspBleBluedroid SPP RSSI=-38 CoD=0x1f00
Inquiry complete (cancelled=0)
```
