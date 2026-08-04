# HealthThermometerServer

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md)

Standard Health Thermometer Service (0x1809) peripheral. Temperature Measurement (0x2A1C) is **indicated** as an IEEE-11073 32-bit FLOAT; Temperature Type (0x2A1D) is readable.

## Hardware

- 1 × original ESP32 running this sketch (peripheral)
- 1 × central: the [HealthThermometerClient](../HealthThermometerClient/) example, or any Health Thermometer collector

## What it does

- Registers the Health Thermometer service before `begin()` and advertises 0x1809
- Every 2 seconds, indicates a slowly rising temperature (36.50–38.49 °C) encoded as an IEEE-11073 FLOAT
- `indicate()` only reaches subscribers, so it is a no-op until a client subscribes

## Key APIs

- `bluetooth.gattServer().addCharacteristic(..., { .indicatable = true })` — Temperature Measurement
- `espBleWriteMedicalFloat32LE(out, mantissa, exponent)` — IEEE-11073 32-bit FLOAT (from `EspBleMedicalFloat.h`)
- `bluetooth.gattServer().indicate(service, characteristic, data, length)` — acknowledged indication

## Expected Serial output

The server is silent; observe the temperature on the client.
