# BatteryClient

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md)

Central that scans for the standard Battery Service (`0x180F`), reads the Battery Level (`0x2A19`), then subscribes to its notifications.

## Hardware

- 1 × original ESP32 running this sketch (central)
- 1 × Battery peripheral: the [BatteryServer](../BatteryServer/) example, or any device exposing the standard Battery Service

## What it does

- Active-scans and connects to the first advertiser offering `0x180F`
- On connect, reads the one-byte Battery Level and prints it
- After the read succeeds, subscribes to Battery Level notifications
- Prints each subsequent level change as a notification arrives

## Key APIs

- `bluetooth.scanner().onResult(...)` / `advertisesService(...)` — pick a peer advertising the service
- `bluetooth.readCharacteristic(...)` + `bluetooth.onCharacteristicRead(...)` — read the current level
- `bluetooth.subscribe(...)` + `bluetooth.onSubscribed(...)` — enable notifications
- `bluetooth.onNotification(...)` — receive level changes

## Expected Serial output

```
Battery: 75%
Battery subscription: ready
Battery changed: 76%
```
