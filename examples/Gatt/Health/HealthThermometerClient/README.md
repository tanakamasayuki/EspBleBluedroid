# HealthThermometerClient

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md)

Connects to a Health Thermometer Service (0x1809), reads Temperature Type, subscribes to Temperature Measurement indications, and decodes the IEEE-11073 32-bit FLOAT temperature.

## Hardware

- 1 × original ESP32 running this sketch (central)
- 1 × Health Thermometer peripheral: the [HealthThermometerServer](../HealthThermometerServer/) example or a commercial thermometer

## What it does

- Scans for and connects to a device advertising 0x1809
- Reads Temperature Type (0x2A1D)
- Subscribes to Temperature Measurement (0x2A1C) **indications** (`subscribe(..., false)`)
- Decodes each measurement's flags + IEEE-11073 FLOAT and prints the temperature

## Key APIs

- `bluetooth.subscribe(connectionId, service, characteristic, false)` — subscribe to indications (not notifications)
- `espBleReadMedicalFloat32LE(bytes)` — decode an IEEE-11073 32-bit FLOAT (from `EspBleMedicalFloat.h`)

## Expected Serial output

```
Temperature type: 2
Temperature: 36.60 C
Temperature: 36.70 C
```
