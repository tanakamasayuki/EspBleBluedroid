# VendorDevice

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

A vendor-defined HID device: an Input, an Output and a Feature report of a caller-chosen size, with bytes the library does not interpret. Unlike the fixed profiles it is bidirectional, so the host can write to the device.

## Hardware

- 1 × original ESP32 running this sketch (HID device / peripheral)
- 1 × HID host that can write a vendor report: a second board running [VendorHost](../VendorHost/), or a host application — `tests/peer/hid_vendor_custom` drives exactly this sketch's shape

## What it does

- Configures the profile with an 8-byte report size before `begin()`
- Raises the MTU to 100, because an ATT payload is MTU − 3
- Prints every Output and Feature report the host writes
- Send `i` to send an 8-byte Input Report

## Three reports, one Report ID

The profile publishes three characteristics that all share UUID `0x2A4D` under Report ID 6, told apart by the type byte in their Report Reference descriptor: Input (notify), Output (write, with or without a response) and Feature (write with a response — it is configuration, so the host wants the acknowledgement).

## Key APIs

- `bluetooth.hidVendor().configure(config)` — register the profile; call **before** `bluetooth.begin()`
- `config.reportSize` — 1..64 bytes, patched into the Report Descriptor
- `vendor.sendInput(data, length)` — one Input Report; `length` must equal `reportSize`
- `vendor.onOutputReport(cb)` / `vendor.onFeatureReport(cb)` — what the host wrote, delivered from `update()`
- `vendor.ready()` — a host is connected, encrypted (when security is on) and subscribed

## Notes

- **The declared size is the only size.** The descriptor fixes it, so `sendInput()` refuses any other length rather than padding or truncating.
- **A report larger than 20 bytes needs a raised MTU.** An ATT payload is MTU − 3, so at the default MTU of 23 anything beyond 20 bytes would be cut off. Both sides have to agree; `config.preferredMtu` is only what this device asks for.
- **`data`/`length` and `rawData`/`rawLength` are the same bytes.** The pair exists so the decoded profiles can put their interpretation in the first pair without hiding the report.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Class, method and callback names | `ble.hidVendor()` | identical |
| Report descriptor | built from its own tables | the same bytes (`tests/unit/hid_report_maps` compares them) |
| Output and Feature report callbacks | queued in the stack callback, dispatched on the next `update()` | dispatched from the same `update()`, because the GATT Server already delivers writes from there |

**How to port:** change the declaration of the library object.

## Expected Serial output

```
Send 'i' to send an 8-byte Vendor Input Report.
Input: sent
Output type=2 length=8 data=01 02 03 04 05 06 07 08
```
