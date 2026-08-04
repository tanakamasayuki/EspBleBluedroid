# BodyCompositionServer

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md)

Standard Body Composition Service (0x181B) peripheral. Body Composition Measurement (0x2A9C) is **indicated** with uint16 flags, the mandatory Body Fat Percentage (0.1 %/LSB), and optional fields; Body Composition Feature (0x2A9B) is a readable uint32.

## Hardware

- 1 × original ESP32 running this sketch (peripheral)
- 1 × central: the [BodyCompositionClient](../BodyCompositionClient/) example, or any Body Composition collector

## What it does

- Registers the Body Composition service before `begin()` and advertises 0x181B
- Every 3 seconds, indicates body fat drifting near 27.5 % plus a fixed 70.000 kg weight (flag bit 10 set)
- `indicate()` only reaches subscribers

## Key APIs

- `bluetooth.gattServer().addCharacteristic(..., { .indicatable = true })` — Body Composition Measurement
- `bluetooth.gattServer().indicate(...)` — acknowledged indication

## Expected Serial output

The server is silent; observe the values on the client.
