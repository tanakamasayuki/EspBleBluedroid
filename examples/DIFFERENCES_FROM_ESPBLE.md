# Differences from EspBle

> 日本語版: [DIFFERENCES_FROM_ESPBLE.ja.md](DIFFERENCES_FROM_ESPBLE.ja.md)

The examples in this directory are ported from the sibling library
[EspBle](https://github.com/tanakamasayuki/EspBle), which targets the ESP32 SoCs
whose BLE backend is NimBLE (S3 / C3 / C6 / H2 / P4). EspBleBluedroid targets the
**original ESP32** — the only member of the family with Bluetooth Classic —
through the Bluedroid backend bundled with Arduino-ESP32.

Where a BLE feature exists in both libraries, the API is deliberately the same,
so most examples port with nothing but a rename. This page lists the differences
that apply library-wide. **Differences that change how a specific example is
written are documented in that example's own README**, under
"Differences from EspBle" / "EspBleとの違い", with the reason and the porting
recipe.

## Always applies

| | EspBle | EspBleBluedroid |
|---|---|---|
| Class / instance | `EspBle ble;` | `EspBleBluedroid bluetooth;` |
| Header | `#include <EspBle.h>` | `#include <EspBleBluedroid.h>` |
| Version macro | `ESPBLE_VERSION_STR` | `ESPBLEBLUEDROID_VERSION_STR` |
| `sketch.yaml` profiles | `esp32s3` (default), `esp32c3`, `esp32c6`, `esp32h2`, `esp32p4` | `esp32` only |
| Build command | `arduino-cli compile --profile esp32s3 …` | `arduino-cli compile --profile esp32 …` |

These four codec headers are byte-for-byte equivalent in both libraries, so
values encoded by one are decoded by the other: `EspBleUuid.h`,
`EspBleIBeacon.h`, `EspBleMedicalFloat.h`, `EspBleCgmCrc.h`.

## Not available here

| Area | Missing | Why | Affected examples |
|---|---|---|---|
| Radio | LE 2M / Coded PHY, `updatePhy()`, `onPhyUpdated()`, `EspBleConnection::txPhy` / `rxPhy` | The original ESP32 radio is Bluetooth 4.2 LE; there is no second PHY to switch to | [Gap/ConnectionParameters](Gap/ConnectionParameters/) |
| Advertising | Extended / Periodic Advertising | The controller implements Legacy Advertising only | — |
| Central | More than one simultaneous connection; `disconnect(id, reason)` | One link is what the peer tests fix while the direct-GATTC migration is in progress; the wrapper does not pass a local reason to link termination | [Gap/Connect](Gap/Connect/) |
| Peripheral | `onConnected()` / `onDisconnected()` / `bluetooth.connection()` for **incoming** links, and BLE security events on a peripheral-only device | Peripheral connection snapshots are not published yet (see [docs/STATUS.ja.md](../docs/STATUS.ja.md)) | [Gap/AcceptList](Gap/AcceptList/), [Gap/DirectedAdvertising](Gap/DirectedAdvertising/), [Gap/PrivateAddress](Gap/PrivateAddress/), [Gatt/Device/BondManagementServer](Gatt/Device/BondManagementServer/), [Security/](Security/) |
| GATT client | `setAutoReconnect()`, `EspBleConfig::persistentSubscriptions`, `setAutoRediscover()` | Cross-link, per-handle state is not fixed by peer tests yet | [Gatt/Basics/AutoReconnectClient](Gatt/Basics/AutoReconnectClient/) |
| HID Host keyboard events | `rawData` / `rawLength` are left empty on a decoded keyboard event | This library carries the report it decoded from, so a sketch reading `event.rawLength` sees 8 here and 0 there (found by `tests/interop/hid`, which asserts it only on this side) | [Hid/](Hid/) |
| Platform | ESP-Hosted SDIO pin overrides | The original ESP32 has its own radio; it is never an ESP-Hosted host | EspBle's `Hosted/CustomPins` has no counterpart here |

### One consequence worth knowing before you wire up a server

Bluedroid stops advertising when a peer connects, and this library does not
deliver a peripheral disconnect event — so **a server example accepts one
connection per boot** unless the sketch restarts advertising itself. Options,
in rough order of how well they behave:

| Approach | Trade-off |
|---|---|
| Restart on a command or a button (`advertising().start()`) | Explicit and safe; the sketch decides when a new peer may connect |
| Restart on a timer after the last observed GATT activity | Works unattended; needs a heuristic for "the peer is gone" |
| Reset the board | Fine for bring-up, not for a product |
| Poll `isAdvertising()` and restart whenever it is false | **Not recommended**: `isAdvertising()` is also false *during* a connection, so this advertises while connected and can admit a second peer, which the single-connection server does not handle |

The Gap and Security peripheral examples in this directory take the first
approach, and say so where it appears in the sketch.

## Same API, different timing or wording

| Area | Behaviour here | Affected examples |
|---|---|---|
| MTU exchange | Started from the first `update()` after the connect worker finishes, because Bluedroid rejects the request from inside its own callback. `onConnected()` still reports 23 | [Gap/Mtu](Gap/Mtu/) |
| Scan results | Advertising and scan-response fields for the same address are merged by the library before delivery, because Bluedroid raises one event per PDU. The result queue holds 16 entries | [Gap/Scan](Gap/Scan/), [Info/ScanDump](Info/ScanDump/) |
| RPA | `localAddress()` returns an empty `String`; the controller does not expose the current RPA | [Gap/PrivateAddress](Gap/PrivateAddress/) |
| GATT client operations | One operation at a time per connection | every `…Client` example |
| GATT client reads | A value longer than one ATT response is still returned whole: Bluedroid continues the read internally, so `result.value` holds everything, as on EspBle. Verified on hardware by `tests/peer/long_value` | [Gatt/Basics/Client](Gatt/Basics/Client/) and every other `…Client` |
| Error detail strings | Wording differs (e.g. `name does not fit in legacy scan response payload`) | [Gap/ScanResponse](Gap/ScanResponse/) |

Resource limits that are **identical** in both libraries, and therefore need no
porting thought: advertising 4 service UUIDs and 4 service data entries per
payload, scan results 8 UUIDs / 4 service data, GATT server 8 services /
32 characteristics / 16 descriptors, discovery snapshot 16 / 48 / 48, accept list
8 entries, preferred MTU default 247.

## Only here

EspBleBluedroid adds Bluetooth Classic, which EspBle cannot have:

- [Classic/](Classic/) — Inquiry, SPP (server / client / `Stream` wrapper / security / passkey), A2DP Sink and Source, HFP Hands-Free and Audio Gateway, profile support table
- [DualMode/](DualMode/) — BLE and Classic traffic at the same time

## Current status

The authoritative list of what is implemented, and of the limits above, is
[docs/STATUS.ja.md](../docs/STATUS.ja.md). The BLE API policy shared with EspBle
is [docs/API_DESIGN_POLICY.ja.md](../docs/API_DESIGN_POLICY.ja.md), and the
backend comparison is
[docs/BLE_BACKEND_DIFFERENCES.ja.md](../docs/BLE_BACKEND_DIFFERENCES.ja.md).
