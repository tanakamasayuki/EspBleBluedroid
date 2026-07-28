# ServiceData

> 日本語版: [README.ja.md](README.ja.md)

Broadcasts a changing binary temperature tagged with its Service UUID.

## Requirements

- One original ESP32
- A scanner capable of displaying Service Data

## Behavior

- Advertises Environmental Sensing Service (`0x181A`)
- Encodes temperature as signed little-endian hundredths of a degree
- Replaces the value and restarts legacy advertising every five seconds

## Main APIs

- `advertising().addServiceData()`
- `setConnectable(false)` / `setScanResponseEnabled(false)`
- Receiver-side `scanResult.serviceDataFor()`

## Notes

Enable `wantDuplicates` on the scanner to observe repeated changes.

## Expected Serial output

```text
Broadcasting 23.50 degC
Broadcasting 23.75 degC
```
