# HeartRateServer

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md)

Standard Heart Rate Service (0x180D) peripheral. Heart Rate Measurement (0x2A37) is **notify-only** and carries an 8-bit heart rate plus one RR interval; Body Sensor Location (0x2A38) is readable (value 1 = Chest).

## Hardware

- 1 × original ESP32 running this sketch (peripheral)
- 1 × central: the [HeartRateClient](../HeartRateClient/) example, or any Heart Rate collector

## What it does

- Registers Heart Rate Measurement (notify) and Body Sensor Location (read) before `begin()` and advertises 0x180D
- Sends `+` / `-` over Serial to increase/decrease the heart rate (1–250 bpm) and notify subscribers
- Emits a variable-length measurement: flags 0x10 (RR interval present), 8-bit bpm, one RR interval

## Key APIs

- `bluetooth.gattServer().addCharacteristic(..., { .notifiable = true })` — Heart Rate Measurement
- `bluetooth.gattServer().setValue(...)` — update the measurement / Body Sensor Location value
- `bluetooth.gattServer().notify(service, characteristic, data, length)` — notify subscribers

## Expected Serial output

```
Send '+' or '-' to change the heart rate and notify subscribers.
Heart rate: 71 bpm (notification accepted: 1)
Heart rate: 72 bpm (notification accepted: 1)
```
