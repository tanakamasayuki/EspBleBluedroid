# BatteryServer

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md)

Peripheral publishing the standard Battery Service (`0x180F`) with a readable and notifiable Battery Level characteristic (`0x2A19`).

## Hardware

- 1 × original ESP32 running this sketch (peripheral)
- 1 × central: the [BatteryClient](../BatteryClient/) example, or any BLE central/phone

## What it does

- Registers the Battery Level characteristic (readable + notifiable) before `begin()` and advertises `0x180F`
- Starts at 75% and logs when a client turns notifications on/off
- Send `+` or `-` over Serial to change the level (clamped 0–100); each change updates the stored value and notifies subscribers

## Key APIs

- `bluetooth.gattServer().addCharacteristic(..., { .readable = true, .notifiable = true })` — declare the Battery Level characteristic
- `bluetooth.gattServer().setValue(...)` — store the current level
- `bluetooth.gattServer().notify(...)` — push the level to subscribers (returns whether it was accepted)
- `bluetooth.gattServer().onSubscriptionChanged(...)` — observe when notifications are enabled/disabled

## Expected Serial output

```
Send '+' or '-' to change the Battery Level.
Battery notifications: 1
Battery: 76% (notification accepted: 1)
```
