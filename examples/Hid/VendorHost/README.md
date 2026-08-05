# VendorHost

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

A BLE HID host (central) for a vendor-defined HID device: scans for the HID service `0x1812`, pairs, discovers, receives Vendor Input reports, and writes Vendor Output / Feature reports. Uses the same post-security `discover()` flow as every other HID report type.

## Hardware

- 1 × original ESP32 running this sketch (HID host / central)
- 1 × HID device: a second board running [VendorDevice](../VendorDevice/)

## What it does

- Scans and connects to the first device advertising the HID service `1812`
- Requests a preferred MTU of 100 and enables security with bonding
- Starts HID discovery from `onSecurityChanged` once encryption succeeds
- Prints incoming Vendor Input reports via `onVendorInput()` (report ID, length, raw bytes)
- Send `o` to write an 8-byte Vendor Output report, `f` to write an 8-byte Vendor Feature report (only while connected)

## Key APIs

- `bluetooth.hidHost().discover(connectionId)` — start HID discovery after security completes
- `hidHost().onDiscovered(cb)` — `EspBleHidKeyboardHostDiscovery` with `success` / `detail`
- `hidHost().onVendorInput(cb)` — `EspBleHidVendorInputEvent` with `reportId`, `rawLength`, `rawData`
- `hidHost().sendVendorOutput(connectionId, data, length)` / `sendVendorFeature(connectionId, data, length)` — write to the device (return success)

## Notes

- **A report has to fit in one ATT payload**, which is `MTU - 3`. `preferredMtu` is what raises it; the default 63-byte vendor report needs far more than the 23-byte default MTU. Writing after `onDiscovered()` is safe on that count: discovery is many round trips, so the exchange has long since finished.
- **The Output and Feature handles come from discovery.** They are read out of the Report Reference descriptors, so `sendVendorOutput()` before `onDiscovered()` has nothing to write to. `hidHost().ready(connectionId)` is the question to ask if the sketch does not track discovery itself.
- `sendVendorOutput()` / `sendVendorFeature()` return false with `InvalidArgument` when the length does not match the size the device declared, and with `NotFound` when the device has no such report. Print `lastErrorName()` to tell the two apart.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Class, method and callback names | `ble.hidHost()` | identical |
| Discovery | issued as a sequence of GATT operations | identical in API; internally one operation at a time, because this backend allows one central GATT operation per link |
| MTU exchange | requested on connect | started from the first `update()` after the connect worker finishes, because Bluedroid rejects the request from inside its own callback. `onConnected()` still reports 23 |
| Simultaneous devices | several connections | one link at a time |

**How to port:** change the declaration of the library object.

## Expected Serial output

```
Send 'o' for Output or 'f' for Feature after discovery.
HID discovery: ready
Vendor Input report=6 length=8 data=45 53 50 00 04 05 06 07
Output: sent
```
