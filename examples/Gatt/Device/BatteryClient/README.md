# BatteryClient

> 日本語版: [README.ja.md](README.ja.md)

Scans for the standard Battery Service (`0x180F`), reads the one-byte Battery
Level (`0x2A19`), then subscribes to its notifications. Request APIs report
acceptance immediately; completion callbacks are delivered later from
`update()`.

The peer can be any standard Battery Service peripheral that supports Battery
Level Read and Notification.

## Requirements

- One original ESP32 running this Central sketch
- A Peripheral advertising Battery Service with readable/notifiable Battery Level

## Behavior

- Reads the one-byte Battery Level after connection
- Subscribes after the Read succeeds
- Prints subsequent Notification values

## Main APIs

- `readCharacteristic()` / `onCharacteristicRead()`
- `subscribe()` / `onSubscribed()` / `onNotification()`

## Expected Serial output

```text
Battery: 75%
Battery subscription: ready
Battery changed: 76%
```
