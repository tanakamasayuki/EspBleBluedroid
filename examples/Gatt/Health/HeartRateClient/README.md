# HeartRateClient

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md)

Connects to a peripheral advertising the standard Heart Rate Service (0x180D), reads Body Sensor Location, and subscribes to Heart Rate Measurement notifications.

## Hardware

- 1 × original ESP32 running this sketch (central)
- 1 × Heart Rate peripheral: the [HeartRateServer](../HeartRateServer/) example, or a commercial heart rate monitor

## What it does

- Scans for and connects to a device advertising 0x180D
- Reads Body Sensor Location (0x2A38)
- Subscribes to Heart Rate Measurement (0x2A37) notifications
- Decodes the measurement per its flags: 8/16-bit heart rate, optional Energy Expended, and multiple RR intervals, validating every variable-length boundary

## Key APIs

- `bluetooth.readCharacteristic(connectionId, service, characteristic)` — read Body Sensor Location
- `bluetooth.subscribe(connectionId, service, characteristic)` — subscribe to notifications
- `bluetooth.onNotification(...)` — receive each Heart Rate Measurement

## Expected Serial output

```
Body Sensor Location: 1
Heart Rate subscription: ready
Heart Rate: 71 bpm, RR intervals: 1 (first: 1024/1024 s)
```
