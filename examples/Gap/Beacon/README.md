# Beacon

> 日本語版: [README.ja.md](README.ja.md)

Broadcasts Manufacturer Data in non-connectable, non-scannable legacy
advertising.

## Requirements

- One original ESP32
- A scanner displaying Manufacturer Data, or [Gap/Scan](../Scan/)

## Behavior

- Disables connections and Scan Responses
- Broadcasts a Company ID and binary payload every 100–150ms

## Main APIs

- `setConnectable(false)` / `setScanResponseEnabled(false)`
- `setManufacturerData()` / `setInterval()`

## Notes

The first two bytes are the little-endian Bluetooth SIG Company ID. `0xFFFF`
is for testing and must be replaced in a product.

## Expected Serial output

The sketch is silent after successful startup. The scanner should show
`ffff01020304`.
