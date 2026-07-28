# ScanResponse

> 日本語版: [README.ja.md](README.ja.md)

Composes the advertising and Scan Response payloads independently.

## Default behavior

Without an explicit Scan Response, a long device name is placed there
automatically. This example assigns both payloads explicitly.

## Requirements

- One original ESP32
- A scanner that can switch between passive and active scanning

## Behavior

- Places the Service UUID, Appearance, and Tx Power in the primary payload
- Places the Local Name and Manufacturer Data in the Scan Response
- Gives passive scanners the primary side and active scanners both sides

## Main APIs

- `advertising().data()` / `advertising().scanResponse()`
- `setAppearance()` / `setTxPowerIncluded()`
- `setName()` / `setManufacturerData()`

## Notes

Each side has an independent 31-byte limit. Flags are added automatically to
the primary payload.

## Expected Serial output

```text
Advertising with an explicit scan response
```
