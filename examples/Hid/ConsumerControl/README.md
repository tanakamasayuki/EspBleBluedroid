# ConsumerControl

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

The media keys of a keyboard as their own HID device: one 16-bit Consumer page usage per report, which is how volume, play/pause and track skip reach a host OS.

## Hardware

- 1 × original ESP32 running this sketch (HID device / peripheral)
- 1 × HID host: a PC, phone or tablet — pair it from the OS Bluetooth settings

## What it does

- Calls `bluetooth.hidConsumerControl().configure()` before `begin()`
- Send `+` for volume up, `-` for volume down, `p` for play/pause

## Key APIs

- `bluetooth.hidConsumerControl().configure()` — register the profile; call **before** `bluetooth.begin()`
- `media.click(usage)` — press and release one usage
- `media.press(usage)` / `release()` / `usage()` — the held usage, one at a time
- `ESP_BLE_HID_CONSUMER_CONTROL_VOLUME_UP` and friends — the usages the descriptor covers

## Notes

- **One usage per report.** The descriptor declares a single 16-bit usage field, so this profile expresses one key at a time; `press()` replaces whatever was held rather than adding to it.
- **The release report matters.** Sending usage 0 is what tells the host the key came up; without it a host may repeat the action. `click()` does both.
- **Any usage the range allows works.** The named constants are the common ones, not the whole set — the descriptor covers the Consumer page, so a usage from the HID usage tables can be passed directly.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Class, method and callback names | `ble.hidConsumerControl()` | identical |
| Report descriptor | built from its own tables | the same bytes (`tests/unit/hid_report_maps` compares them) |

**How to port:** change the declaration of the library object.

## Expected Serial output

```
Send '+', '-', or 'p'.
```
