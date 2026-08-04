# CurrentTimeServer

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md)

Standard Current Time Service (0x1805) peripheral. Current Time (0x2A2B) is a read/**notify** characteristic holding the standard 10-byte wire format (year LE, month, day, hours, minutes, seconds, weekday, fraction256, adjust reason).

## Hardware

- 1 × original ESP32 running this sketch (peripheral)
- 1 × central: the [CurrentTimeClient](../CurrentTimeClient/) example, or any Current Time client

## What it does

- Registers Current Time before `begin()` and advertises 0x1805
- Seeds the value with a fixed demo time (2026-07-19 12:34:56, Sunday, manually adjusted)
- Send `t` over Serial to advance the seconds field by one, then set the value and notify subscribers

## Key APIs

- `bluetooth.gattServer().setValue(...)` — update the stored Current Time value
- `bluetooth.gattServer().notify(...)` — notify subscribers of the new time

## Expected Serial output

```
Send 't' to advance one second and notify subscribers.
Time: 2026-07-19 12:34:57 (notification accepted: 1)
```
