# IBeacon

> 日本語版: [README.ja.md](README.ja.md)

Broadcasts an Apple iBeacon as non-connectable, non-scannable advertising.
The backend-independent `EspBleIBeacon.h` codec is shared with EspBle.

Fill `EspBleIBeaconData` with a proximity UUID, major, minor, and calibrated
RSSI at one metre, then call `espBleEncodeIBeacon()` to build the 25-byte
Manufacturer Data. A scanner can use `espBleIsIBeacon()` and
`espBleDecodeIBeacon()` to recognize and decode it.
