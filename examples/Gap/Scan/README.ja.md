# Scan

> English: [README.md](README.md)

継続的なActive Scanを行い、address、RSSI、Local Nameを表示します。

## 必要なもの

- 無印ESP32 × 1
- 周囲のBLE機器。対になる送信例は[Gap/Advertise](../Advertise/)

## 動作

- Active ScanでScan Responseも要求します
- 受信結果を値型へcopyします
- callbackをBluedroid taskではなく`update()`を呼んだcontextから配送します

## 主なAPI

- `scanner().onResult()` — `EspBleScanResult`を受け取る
- `EspBleScanConfig::active` — Active/Passive Scanを選択
- `intervalMilliseconds` / `windowMilliseconds` / `durationSeconds`
- `scanResult.hasName()` — nameを持たないpacketと区別

## 期待されるSerial出力

```text
00:11:22:33:44:55 RSSI=-48 name=EspBle Advertiser
```
