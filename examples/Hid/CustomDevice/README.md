# CustomDevice

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

A HID device with an **arbitrary** Report Descriptor, via `bluetooth.hidCustom()`. This example is a vendor-defined "control panel": a 2-byte input report (a signed dial delta plus a button bitfield) and a 1-byte output report (an LED state written by the host).

## Hardware

- 1 × original ESP32 running this sketch (HID device / peripheral)
- 1 × HID host that speaks this descriptor: a host application, or a second board acting as a generic GATT client — `tests/peer/hid_vendor_custom` drives a device of exactly this shape

## What it does

- `configure()`, then `addInputReport()` / `addOutputReport()` per report, then `setReportMap()` — all before `begin()`
- Prints the 1-byte output report whenever the host writes it
- Send `i` to send a 2-byte input report (dial +5, button 1)

## The descriptor is yours; the plumbing is not

The library publishes the bytes you pass to `setReportMap()` as the Report Map (`0x2A4B`) and gives each report you declared its own `0x2A4D` characteristic with a Report Reference (`{report id, type}`). What it does **not** do is check that the two agree — the descriptor is the contract with the host OS, so the report IDs and sizes you declare have to be the ones the descriptor describes.

Custom reports are composed into the same HID service as the fixed profiles, so a custom report can coexist with `hidKeyboard()` / `hidMouse()`: the Report Map is then the composed built-in descriptors followed by yours. Report IDs 1..6 are reserved only while the matching built-in profile is also enabled.

## Key APIs

- `bluetooth.hidCustom().configure()` — bring up the HID service; call **before** `bluetooth.begin()`
- `custom.setReportMap(bytes, length)` — the raw Report Descriptor, up to 256 bytes
- `custom.addInputReport(id, size)` / `addOutputReport()` / `addFeatureReport()` — up to `EspBleHidCustom::MaxReports` (4) in total
- `custom.sendInput(id, data, length)` — `length` must equal the declared size
- `custom.onOutputReport(cb)` / `onFeatureReport(cb)` — what the host wrote, delivered from `update()`
- `custom.ready(id)` — per Input Report, because a host subscribes to each separately

## Notes

- **Declare before `begin()`.** Each report becomes an attribute, and the attribute table is built once.
- **An undeclared report ID is not a report.** `sendInput()` fails with `NotFound` rather than inventing a characteristic.
- **A report larger than 20 bytes needs a raised MTU** (`config.preferredMtu`), because an ATT payload is MTU − 3.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Class, method and callback names | `ble.hidCustom()` | identical |
| Report Map composition | built-in descriptors, then the custom one | identical |
| `MaxReports` | 4 | 4 |
| Output and Feature report callbacks | queued in the stack callback, dispatched on the next `update()` | dispatched from the same `update()`, because the GATT Server already delivers writes from there |

**How to port:** change the declaration of the library object.

## Expected Serial output

```
Send 'i' to send an input report (dial +5, button 1).
Output report id=1 len=1 value=2
```
