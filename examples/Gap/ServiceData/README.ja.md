# ServiceData

> English: [README.md](README.md)

接続しないbroadcasterから、Service UUIDで意味づけたbinary温度値を放送します。

## 必要なもの

- 無印ESP32 × 1
- Service Dataを表示できるscanner

## 動作

- Environmental Sensing Service（`0x181A`）をAdvertisingします
- 0.01度単位のsigned 16 bit値をlittle-endianで載せます
- 5秒ごとに値を置換し、Legacy Advertisingを再開します

## 主なAPI

- `advertising().addServiceData()` — UUID付きbinary payloadを追加・置換
- `setConnectable(false)` / `setScanResponseEnabled(false)` — pure broadcaster
- `scanResult.serviceDataFor()` — 受信側でUUIDを指定して値を取得

## 注意

継続的な変化を受信するにはscannerで`wantDuplicates = true`を指定します。
Service Dataも31 byte上限の対象で、128 bit UUIDはUUIDだけで16 byte使います。

## 期待されるSerial出力

```text
Broadcasting 23.50 degC
Broadcasting 23.75 degC
```
