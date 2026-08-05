# Mouse

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

A BLE HID mouse over GATT (HOGP): a relative-motion pointer with buttons and a wheel. Advertises the HID service `0x1812`; motion and clicks are triggered by Serial commands.

## Hardware

- 1 × original ESP32 running this sketch (HID mouse / peripheral)
- 1 × HID host: a PC, phone or tablet — pair it from the OS Bluetooth settings

## What it does

- Calls `bluetooth.hidMouse().configure()` before `begin()`, which composes the same HID, Battery and Device Information services a keyboard does — with only the mouse descriptor in the Report Map
- Declares three buttons instead of the default five: the count is patched into the Report Descriptor, and the report stays 4 bytes whatever it is
- Send `m` to move (+12, -8), `c` to click, `w` to scroll, `d` to drag

## Key APIs

- `bluetooth.hidMouse().configure(config)` — register the profile; call **before** `bluetooth.begin()`
- `mouse.move(dx, dy, wheel)` — one relative-motion report
- `mouse.press()` / `release()` / `releaseAll()` / `buttons()` — the held-button state
- `mouse.click(ESP_BLE_HID_MOUSE_LEFT)` — press and release
- `mouse.wheel(amount)` — scroll without moving
- `mouse.ready()` — a host is connected, encrypted (when security is on) and subscribed

## Notes

- **A drag is a move with the button still down.** `move()` keeps whatever buttons are held, so a drag is `press()`, then `move()`, then `releaseAll()` — the button state does not have to be repeated on every report.
- **Every value is a delta, not a position.** The HID report carries signed 8-bit movement, so a long travel is several reports rather than one large one.
- **Security is effectively required.** With it on, the device asks the host to pair as soon as it connects, and the HID attributes also answer an unencrypted read with an insufficient-encryption error (`tests/peer/hid_security`).

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Class, method and callback names | `ble.hidMouse()` | identical |
| Report descriptor | built from its own tables | the same bytes (`tests/unit/hid_report_maps` compares them) |

**How to port:** change the declaration of the library object.

## Expected Serial output

```
Send 'm' to move, 'c' to click, 'w' to scroll, 'd' to drag.
```
