# UserDataClient

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md)

Connects to a User Data Service (0x181C), subscribes to Database Change Increment notifications, reads Age, and writes a new First Name and Age. Each write bumps the increment, which arrives as a notification.

## Hardware

- 1 × original ESP32 running this sketch (central)
- 1 × User Data peripheral: the [UserDataServer](../UserDataServer/) example

## What it does

- Scans for and connects to a device advertising 0x181C
- Subscribes to Database Change Increment (0x2A99) **notifications**
- Reads Age (0x2A80), then writes First Name (0x2A8A) = "Ada" and Age = 42 **with response**
- Prints each Database Change Increment notification as the server bumps it

## Key APIs

- `bluetooth.writeCharacteristic(connectionId, service, characteristic, data, length, true)` — write with response

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Concurrent GATT operations | more than one may be issued | **one at a time per connection**; the second is rejected with `InvalidState` |

**Why:** the GATT client still runs one operation at a time while the direct-GATTC migration is in progress (see [docs/STATUS.ja.md](../../../../docs/STATUS.ja.md)).

**How to port:** chain the writes. This example writes First Name from `onCharacteristicRead()` and Age from `onCharacteristicWritten()`, instead of issuing both in one callback.

## Expected Serial output

```
Age: 25
Database Change Increment: 1
Database Change Increment: 2
```
