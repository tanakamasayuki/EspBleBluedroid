# Scan

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 2, "GAP"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

Runs a continuous active scan and prints every advertisement it receives: address, RSSI, and the device name when present. A **minimal** central example; pair it with the [Advertise](../Advertise/) example on a second board, or just observe nearby BLE devices.

Those three fields are all it prints. To see **every field** — service UUIDs, service data, manufacturer data, decoded iBeacons — use [Info/ScanDump](../../Info/ScanDump/) instead. This example stays focused on the smallest way to start a scan and receive results.

## Hardware

- 1 × original ESP32 running this sketch (central)
- Optional peer — the [Advertise](../Advertise/) example on a second board, or any nearby BLE device

## What it does

- Starts an active scan with no duration limit (`durationSeconds = 0`)
- Delivers each result as a value-type copy from the `bluetooth.update()` context — the callback never runs on the BLE stack task
- Prints address, RSSI, and name (when present) for every result

## Key APIs

- `bluetooth.scanner().onResult(callback)` — receives an `EspBleScanResult` per advertisement
  - `scanResult.address`, `scanResult.rssi`, `scanResult.hasName()`, `scanResult.name`
  - also available: `advertisesService(uuid)`, `connectable`, manufacturer data
- `EspBleScanConfig` — `active`, `wantDuplicates`, `intervalMilliseconds`, `windowMilliseconds`, `durationSeconds`, `acceptListOnly`
  - With `acceptListOnly = true` only advertisers registered through `bluetooth.addToAcceptList()` are reported. The controller drops the rest, so they never reach `onResult` ([Gap/AcceptList](../AcceptList/) uses the same list to restrict connections). Matching is by address, so a peer that rotates an RPA must be bonded first
- `bluetooth.scanner().start(scanConfig)` / `bluetooth.scanner().stop()`
- `bluetooth.scanner().droppedResultCount()` — results dropped when the queue overflows

## Notes

- **Advertising and scan response are merged per address.** Bluedroid sometimes reports the advertising payload on its own before the scan response arrives. For a short window the library holds the result for that address, merges the scan-response fields into it, and only then delivers one `EspBleScanResult` to the callback. This is why an active scan reports the name even when the name lives in the scan response.
- **The result queue holds 16 entries.** Results arrive on the BLE stack task, are copied into the queue, and are delivered from `update()`. If a sketch stops calling `update()` (or blocks in the callback), the queue overflows and the excess is counted by `droppedResultCount()`.
- `end()` discards results still waiting in the queue without delivering them.
- Classic device discovery is a **separate** operation with its own result type ([Classic/Inquiry](../../Classic/Inquiry/)); running it at the same time as a BLE scan is not guaranteed.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| `onResult()` delivery | value copy, from `update()` | identical |
| Result queue | bounded, `droppedResultCount()` | 16 entries, `droppedResultCount()` |
| Advertising / scan-response merge | done by the backend | done by the library, per address, within a short window |
| Companion operation | — | Bluetooth Classic Inquiry ([Classic/Inquiry](../../Classic/Inquiry/)) |

**Why:** Bluedroid raises one GAP result event per PDU, so an active scan can surface the advertising payload before the scan response for the same advertiser. Delivering both would make the same device appear twice with different fields, so the library merges them by address before handing a single result to the application.

**How to port:** no code change.

## Expected Serial output

```
5a:b8:1e:0c:2f:71 RSSI=-52 name=EspBleBluedroid Advertiser
70:04:1d:32:99:a0 RSSI=-78
...
```
