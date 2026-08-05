# KeyboardNkro

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

N-key rollover: the whole keyboard state travels as one report — a modifier byte plus a 224-bit usage bitmap — so any number of keys can be held at once instead of the six a boot-compatible report carries.

## Hardware

- 1 × original ESP32 running this sketch (HID keyboard / peripheral)
- 1 × HID host: a PC, phone or tablet — pair it from the OS Bluetooth settings

## What it does

- Calls `enableNkro()` **before** `configure()`, which selects the NKRO descriptor for the Report Map
- Raises the MTU: a 29-byte Input Report needs an ATT payload of 29, i.e. MTU ≥ 32
- Send `n` for eight simultaneous keys, `r` to release all

## Key APIs

- `keyboard.enableNkro()` — before `configure()`; afterwards it has no effect on the published descriptor
- `keyboard.sendReport(EspBleHidKeyboardNkroReport)` — the whole state in one notification
- `report.press(usage)` / `release(usage)` / `isDown(usage)` / `clear()` — the bitmap, with modifier usages (0xE0–0xE7) routed to the modifier byte
- `keyboard.heldState()` — the state the host was last told about
- `keyboard.pressUsage()` / `releaseUsage()` — incremental changes to that same state

## Notes

- **A 6-key report is not the NKRO report.** `sendReport(EspBleHidKeyboardReport)` carries `keys[6]`, so it expresses only six usages even with NKRO enabled — it is expanded into the bitmap and sent in the NKRO layout. For more than six keys at once, build an `EspBleHidKeyboardNkroReport`.
- **One report beats several.** `pressUsage()` can hold eight keys too, but each change is its own notification, paced by the connection interval; a whole-state report is one packet.
- **The bitmap is not a usage array.** `press(0x04)` sets bit 4, not `bitmap[0] = 4`. Usages above `MaxBitmapUsage` (0xDF) that are not modifiers cannot be represented, and `press()` returns false for them.
- **NKRO and Boot Protocol coexist.** A host in Boot Protocol Mode gets the fixed 8-byte Boot Keyboard report, down-converted from the bitmap (with the HID rollover code 0x01 when too many keys are held).

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Class, method and callback names | `ble.hidKeyboard()` | identical |
| NKRO descriptor and report layout | its own 29-byte layout | the same bytes (`tests/unit/hid_report_maps` compares them) |

**How to port:** change the declaration of the library object.

## Expected Serial output

```
Send 'n' for eight simultaneous keys, 'r' to release all.
No subscribed HID Host yet.
```
