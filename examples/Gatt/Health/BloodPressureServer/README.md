# BloodPressureServer

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md)

Standard Blood Pressure Service (0x1810) peripheral. Blood Pressure Measurement (0x2A35) is **indicated** with systolic/diastolic/mean as IEEE-11073 16-bit SFLOATs; Blood Pressure Feature (0x2A49) is readable.

## Hardware

- 1 × original ESP32 running this sketch (peripheral)
- 1 × central: the [BloodPressureClient](../BloodPressureClient/) example, or any Blood Pressure collector

## What it does

- Registers the Blood Pressure service before `begin()` and advertises 0x1810
- Every 3 seconds, indicates a 120/80 mmHg reading (mean 93) encoded as SFLOATs
- `indicate()` only reaches subscribers

## Key APIs

- `espBleWriteMedicalSFloatLE(out, mantissa, exponent)` — IEEE-11073 16-bit SFLOAT (from `EspBleMedicalFloat.h`)
- `bluetooth.gattServer().indicate(...)` — acknowledged indication

## Expected Serial output

The server is silent; observe the reading on the client.
