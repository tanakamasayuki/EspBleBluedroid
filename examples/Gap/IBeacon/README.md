# IBeacon

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 2, "GAP"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

Broadcasts an Apple iBeacon: a non-connectable, non-scannable advertisement whose manufacturer data carries a proximity UUID, major, minor, and measured power. The layout is built by the backend-independent `EspBleIBeacon.h` codec.

## Hardware

- 1 × original ESP32 running this sketch (broadcaster)
- A BLE scanner — the [Scan](../Scan/) example on a second board, or a beacon app such as nRF Connect / Locate Beacon

## What it does

- `espBleEncodeIBeacon(...)` builds the 25-byte iBeacon manufacturer data (Apple company ID `0x004C`, type `0x02`, length `0x15`, UUID, major, minor, measured power)
- Broadcasts it as a pure, non-scannable advertisement on the configured interval

## Key APIs

- `EspBleIBeaconData` — proximity `uuid[16]`, `major`, `minor`, `measuredPower`
- `espBleEncodeIBeacon(beacon, out)` — encode to `EspBleIBeaconManufacturerDataSize` (25) octets
- `espBleDecodeIBeacon(manufacturerData, length, beacon)` / `espBleIsIBeacon(...)` — decode/validate on the scanner side
- `bluetooth.advertising().setConnectable(false)` / `setScanResponseEnabled(false)` / `setManufacturerData(...)` / `setInterval(...)`

## Notes

- `measuredPower` is the calibrated RSSI at 1 m (typically negative, e.g. -59), used by receivers to estimate distance.
- Replace the UUID / major / minor with your own values.

## Expected Serial output

Silent on success. On failure:

```
BLE init failed: INVALID_STATE (...)
Advertising failed: INVALID_ARGUMENT (...)
```
