# BloodPressureClient

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md)

Connects to a Blood Pressure Service (0x1810), reads Blood Pressure Feature, subscribes to Blood Pressure Measurement indications, and decodes the systolic/diastolic/mean IEEE-11073 SFLOAT values.

## Hardware

- 1 × original ESP32 running this sketch (central)
- 1 × Blood Pressure peripheral: the [BloodPressureServer](../BloodPressureServer/) example or a commercial monitor

## What it does

- Scans for and connects to a device advertising 0x1810
- Reads Blood Pressure Feature (0x2A49)
- Subscribes to Blood Pressure Measurement (0x2A35) **indications** (`subscribe(..., false)`)
- Decodes each measurement's SFLOAT values and prints the reading

## Key APIs

- `bluetooth.subscribe(connectionId, service, characteristic, false)` — subscribe to indications
- `espBleReadMedicalSFloatLE(bytes)` — decode an IEEE-11073 16-bit SFLOAT (from `EspBleMedicalFloat.h`)

## Expected Serial output

```
Blood pressure: 120/80 mmHg (mean 93)
```
