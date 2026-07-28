# ScanDump

> English: [README.md](README.md)

EspBleBluedroidがAdvertisingとScan Responseから取り出す全公開fieldを1行ずつ表示する
診断用scannerです。filterを書く前にpeerの実際のpayloadを確認できます。

## 必要なもの

- 無印ESP32 × 1
- 調査対象のBLE機器

## 動作

- address種別、RSSI、connectable/scannable、nameを表示します
- Appearance、Tx Power、全Service UUID、Service Data、Manufacturer Dataを表示します
- `d`で重複報告を切り替え、`q`でdrop counterを表示します

## 主なAPI

- `EspBleScanResult`の全公開fieldと`has*()` helper
- `EspBleScanConfig::wantDuplicates`
- `scanner().droppedResultCount()` / `droppedEventCount()`

Manufacturer DataとService Dataはbinary-safeな`String`をhex表示します。
