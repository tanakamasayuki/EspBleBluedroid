# Beacon

> English: [README.md](README.md)

Manufacturer Dataをnon-connectable・non-scannableなLegacy Advertisingで放送します。

## 必要なもの

- 無印ESP32 × 1
- Manufacturer Dataを表示できるscanner、または[Gap/Scan](../Scan/)

## 動作

- 接続を受け付けないpure broadcasterにします
- Scan Requestへ応答しない構成にします
- Company IDとbinary payloadを100〜150ms間隔で送ります

## 主なAPI

- `setConnectable(false)` / `setScanResponseEnabled(false)`
- `setManufacturerData()` — binary payloadを設定
- `setInterval()` — Advertising Interval範囲をmsで指定

## メモ

先頭2 byteはlittle-endianのBluetooth SIG Company IDです。`0xFFFF`はテスト用なので、
製品では割り当て済みIDへ置き換えてください。

## 期待されるSerial出力

正常開始時に定期出力はありません。scanner側で`ffff01020304`を確認してください。
