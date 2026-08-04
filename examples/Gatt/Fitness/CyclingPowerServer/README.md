# CyclingPowerServer

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md)

Standard Cycling Power Service (0x1818) peripheral. Cycling Power Measurement (0x2A63) is notified with 16-bit flags and a signed 16-bit instantaneous power (watts); Cycling Power Feature (0x2A65) and Sensor Location (0x2A5D) are readable.

## Hardware

- 1 × original ESP32 running this sketch (peripheral)
- 1 × central: the [CyclingPowerClient](../CyclingPowerClient/) example, or any Cycling Power collector

## What it does

- Registers the Cycling Power service and advertises 0x1818
- Publishes Cycling Power Feature (0x0000000C) and Sensor Location = Right Crank (6) as readable values
- Every second, ramps the power 200..300 W (wrapping back to 200) and notifies a 4-byte Cycling Power Measurement with 16-bit flags 0x0000 (no optional fields)

## Key APIs

- `bluetooth.gattServer().addCharacteristic(..., { .notifiable = true })` — Cycling Power Measurement
- `bluetooth.gattServer().setValue(...)` — seed the readable Feature / Sensor Location
- `bluetooth.gattServer().notify(...)` — push each measurement to subscribers

## Notes

- Instantaneous power is a signed 16-bit little-endian value in watts, immediately after the 16-bit flags field.

## Expected Serial output

```
The server is silent; observe values on the client.
```
