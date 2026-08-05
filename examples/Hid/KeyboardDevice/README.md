# KeyboardDevice

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

Turns the board into a BLE HID keyboard (HID over GATT, fixed 6KRO). Paired from a PC or phone it types like a real keyboard; keystrokes are triggered by Serial commands. The host's LED output report (Num/Caps/Scroll Lock) is printed as it arrives.

## Hardware

- 1 × original ESP32 running this sketch (HID keyboard / peripheral)
- 1 × HID host: a PC, phone or tablet — pair it from the OS Bluetooth settings

## What it does

- Registers the HID, Battery and Device Information services before `begin()`, and adds the HID service UUID and the keyboard appearance to advertising
- Types Shift+A on Serial command `a` and releases every key on `r`
- Prints the LED state whenever the host writes it, and the Protocol Mode whenever the host switches it
- Restarts advertising after each disconnect, so the host can reconnect from its bond

## Two Report characteristics, one UUID

HOGP puts the input and the output report in two characteristics that **share UUID 0x2A4D**, told apart by their Report Reference descriptor (`{report id, type}`). That is why this profile needs a GATT Server able to publish duplicate characteristic UUIDs; `tests/peer/duplicate_uuid_server` pins that, and `tests/peer/hid_keyboard_device` checks that a host really can tell the two apart.

## Key APIs

- `bluetooth.hidKeyboard()` — the keyboard profile of this device
- `keyboard.configure(config)` — register the services; call **before** `bluetooth.begin()`
- `keyboard.ready()` — a host is connected, encrypted (when security is on) and subscribed
- `keyboard.sendReport(report)` / `releaseAll()` — modifier byte plus up to six usages
- `keyboard.pressKey(char)` / `tapKey(char)` / `write(text)` — the layout does the character → usage lookup
- `keyboard.enableNkro()` before `configure()` — n-key rollover, then `sendReport(EspBleHidKeyboardNkroReport)`
- `keyboard.onOutputReport(callback)` / `keyboard.ledState()` — the host's LED state, as an event or as a question
- `keyboard.onProtocolMode(callback)` / `keyboard.protocolMode()` — Report or Boot Protocol
- `keyboard.setBatteryLevel(level)` — read by the host, and notified if it subscribed

## Notes

- **`ready()` is not "connected".** A HOGP host reads the descriptors, pairs, and only then subscribes to the input report; until it does, `sendReport()` fails with `InvalidState` and a detail naming which of the two it is (`no connected HID Host` / `no subscribed HID Host`). Poll `ready()` rather than inferring connectivity from a send result.
- **Security is effectively required.** A host OS starts pairing because the HID attributes answer an unencrypted read with an insufficient-encryption error. With `security.enabled` off, the attributes are readable and some hosts will simply not pair.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Class, method and callback names | `ble.hidKeyboard()` | identical |
| Report descriptors | built from its own tables | the same bytes (`tests/unit/hid_report_maps` compares them) |
| `onOutputReport()` and `ledState()` | the state is updated when the host writes, the callback can arrive an `update()` later | both happen in the same `update()`, because the GATT Server already dispatches writes from there |
| Profiles available | keyboard, mouse, consumer, system, gamepad, vendor, custom, host | every device profile; the HID **host** side is listed in `docs/API_PARITY.tsv` as `planned` |

**How to port:** change the declaration of the library object. A keyboard sketch needs no other change.

## Expected Serial output

```
Send 'a' to type Shift+A and 'r' to release all keys.
Protocol Mode: Report
Keyboard LEDs: num=0 caps=1 scroll=0
```
