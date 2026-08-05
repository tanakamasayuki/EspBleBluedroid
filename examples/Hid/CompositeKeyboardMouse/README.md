# CompositeKeyboardMouse

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

One device that is both a keyboard and a mouse. HOGP gives a device a single HID service, so both profiles share it and the reports are told apart by Report ID.

## Hardware

- 1 × original ESP32 running this sketch (HID device / peripheral)
- 1 × HID host: a PC, phone or tablet — pair it from the OS Bluetooth settings

## What it does

- Configures both profiles before `begin()`, because the Report Map is composed once from whatever has been registered by then
- Send `h` to type "hello", `m` to move the pointer, `?` to print each profile's `ready()`

## One service, one Report Map

The device publishes **one** HID service with **one** Report Map holding both descriptors, and one Input Report characteristic per profile — all of them UUID `0x2A4D`, told apart by their Report Reference descriptor (`{report id, type}`). The host uses the report ID to route each notification. `tests/peer/hid_composite` checks this on the air with all five notify-only profiles at once.

## Key APIs

- `bluetooth.hidKeyboard().configure()` / `bluetooth.hidMouse().configure()` — both **before** `bluetooth.begin()`
- `keyboard.write(text)` / `mouse.move(dx, dy)` — each profile's own reports
- `keyboard.ready()` / `mouse.ready()` — per profile, because a host subscribes to each Input Report separately

## Notes

- **`ready()` is a per-profile question.** Each profile has its own CCCD, so a host may be listening to the keyboard and not to the mouse. Poll the profile you are about to use.
- **The order of `configure()` calls is visible on the air**, but only in the attribute order. The descriptor order inside the Report Map is fixed (keyboard, mouse, gamepad, consumer, system, vendor), and a host uses neither — it uses the Report Reference.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Class, method and callback names | `ble.hidKeyboard()`, `ble.hidMouse()` | identical |
| Report descriptors | built from its own tables | the same bytes (`tests/unit/hid_report_maps` compares them) |

**How to port:** change the declaration of the library object.

## Expected Serial output

```
Send 'h' to type hello, 'm' to move the pointer.
ready: keyboard=1 mouse=1
```
