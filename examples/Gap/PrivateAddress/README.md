# PrivateAddress

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 2, "GAP"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

Advertises with a private address instead of the factory public address, selected via `EspBleConfig::ownAddressType`. A connectable peripheral example; observe the address type with the paired [Scan](../Scan/) example.

## Hardware

- 1 × original ESP32 running this sketch (peripheral)
- A BLE scanner — the [Scan](../Scan/) example on a second board, or a scanner app such as nRF Connect

## The two modes

Switch with `USE_RESOLVABLE_PRIVATE_ADDRESS` at the top of the sketch.

| | RandomStatic (default) | ResolvablePrivate (RPA) |
|---|---|---|
| Address | A fixed random address generated at `begin()` | Rotated periodically by the controller |
| Tracking resistance | Hides the public address, but the fixed value is still trackable | Rotates, so it is hard to track |
| Bonding | Not needed | **Required**; the peer resolves the address with the IRK |
| Works standalone | Yes | No — without security the peer cannot reconnect |

RPA privacy is controller-managed. The rotation period is decided by the Bluedroid controller and cannot be changed from the application.

## What it does

- Sets `config.ownAddressType` for the selected mode; the RPA mode also enables `config.security.enabled` / `bonding`
- Advertises a connectable peripheral so a scanner can observe the address type
- Prints the address this device published and its address type

## Key APIs

- `EspBleConfig::ownAddressType` — `Public` (default) / `RandomStatic` / `ResolvablePrivate`
- `EspBleConfig::security.enabled` / `bonding` — required when using an RPA
- `bluetooth.localAddress()` / `localAddressType()` — the address published on air. **Empty for RPA** (see the differences below)

## Notes

- `Public` — the factory public address. `RandomStatic` — a random static address generated at `begin()`; a fixed identity that does not rotate. `ResolvablePrivate` — a Resolvable Private Address (RPA) the controller rotates on its own timer; only useful with security/bonding, since a bonded peer resolves it via the IRK, while an unbonded scanner sees only a changing random address.
- A scanner sees this device with a **Random** address type (not Public). A static random address has its top two most-significant bits set (`0b11`) in the top octet.
- Extended and Periodic Advertising are not available; the original ESP32 controller implements Legacy Advertising only.
- Scan Requests still go out with the Public address. The private address applies to the advertising identity.
- The accept list matches by address, so a peer using an RPA cannot be listed until it is bonded and its identity address applies (see [AcceptList](../AcceptList/)).

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| `localAddress()` with an RPA | returns the current RPA | returns an empty `String` |
| RPA rotation period | fixed by the backend build | decided by the controller |
| Peripheral-side connect / disconnect callbacks | delivered | **not delivered** — this sketch prints the published address instead |
| Extended / Periodic Advertising | not available | not available (no controller support) |

**Why:** the original ESP32 controller generates its RPA internally and its supported GAP API offers no call that reads the current value back, so the library returns an empty `String` rather than a stale or invented address. And `onConnected()` / `onDisconnected()` in EspBleBluedroid describe links this device opened with `connect()`; incoming peripheral links are not yet surfaced as connection snapshots (see [docs/STATUS.ja.md](../../../docs/STATUS.ja.md)).

**How to port:** treat an empty `localAddress()` as "controller-managed" instead of an error, and observe the connection from the central side ([Gap/Scan](../Scan/), [Info/ScanDump](../../Info/ScanDump/)) rather than from peripheral callbacks.

## Expected Serial output

```
Advertising with a random static address; current=c7:41:9b:2e:55:8a type=1
```
