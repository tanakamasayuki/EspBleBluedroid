# ScanResponse

> English: [README.md](README.md)

Advertising本体とScan Responseを別々に構成し、2つの31 byte枠を使い分けます。

## 既定の動作との関係

Scan Responseを明示構成しない場合、長いdevice nameは自動的にScan Responseへ
配置されます。このexampleでは両面をapplicationが明示します。

## 必要なもの

- 無印ESP32 × 1
- Passive/Active Scanを切り替えられるscanner

## 動作

- Advertising本体へService UUID、Appearance、Tx Powerを載せます
- Scan ResponseへLocal NameとManufacturer Dataを載せます
- Passive scannerには本体だけ、Active scannerには両面が届きます

## 主なAPI

- `advertising().data()` — Advertising本体のbuilder
- `advertising().scanResponse()` — Scan Responseのbuilder
- `setAppearance()` / `setTxPowerIncluded()` / `setName()` / `setManufacturerData()`

## 注意

FlagsはAdvertising本体へ自動追加されます。各AD構造にはlengthとtypeの2 byteも必要です。
どちらかが31 byteを超えると`start()`は`InvalidArgument`で失敗します。

## 期待されるSerial出力

```text
Advertising with an explicit scan response
```
