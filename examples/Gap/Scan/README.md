# Scan

> 日本語版: [README.ja.md](README.ja.md)

Runs a continuous active scan and prints each address, RSSI, and Local Name.

## Requirements

- One original ESP32
- Nearby BLE devices; [Gap/Advertise](../Advertise/) is a matching transmitter

## Behavior

- Requests Scan Responses through Active Scan
- Copies backend results into value types
- Delivers callbacks from `update()`, never directly from the Bluedroid task

## Main APIs

- `scanner().onResult()`
- `EspBleScanConfig::active`, interval, window, duration, and `acceptListOnly`
- `EspBleScanResult::hasName()`

## Expected Serial output

```text
00:11:22:33:44:55 RSSI=-48 name=EspBle Advertiser
```
