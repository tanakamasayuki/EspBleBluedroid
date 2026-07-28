# Advertise

> 日本語版: [README.ja.md](README.ja.md)

Broadcasts a Local Name, Service UUID, and Manufacturer Data in connectable
legacy advertising.

## Requirements

- One original ESP32
- [Gap/Scan](../Scan/) or a generic BLE scanner

## Behavior

- Configures the GAP device name before `begin()`
- Adds a Local Name, Battery Service UUID, and binary Manufacturer Data
- Starts advertising and calls `update()` continuously

## Main APIs

- `EspBleConfig::deviceName`
- `advertising().setName()` / `addServiceUuid()` / `setManufacturerData()`
- `advertising().start()`

`start()` rejects either legacy payload if it exceeds 31 bytes.

## Expected Serial output

```text
advertising
```
