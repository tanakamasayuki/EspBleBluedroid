# BondManagementServer

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md)

Standard Bond Management Service (0x181E) peripheral. Bond Management Feature (0x2AA5) is a readable uint24 bit field of supported operations; the Bond Management Control Point (0x2AA4) is writable and receives op codes in `onWritten`.

## Hardware

- 1 × original ESP32 running this sketch (peripheral)
- 1 × central: the [BondManagementClient](../BondManagementClient/) example, or any Bond Management client

## What it does

- Registers the Control Point and Feature before `begin()` and advertises 0x181E
- Advertises support for "Delete all bonds on server (LE)" (bit 10) → `0x000400`
- On op code 0x03 or 0x06, schedules a delete of **every LE bond** three seconds later, so the client's own disconnect happens first
- Runs the delete from `loop()` and prints how many bonds went away

## Why the whole store, not one peer

The service distinguishes "delete the bond of the device that is asking" from
"delete every bond". Honouring the first needs the **address of the peer that
wrote the Control Point** — and on this library a peripheral link has no
connection snapshot, so that address is not available (see
[docs/STATUS.ja.md](../../../../docs/STATUS.ja.md)).

Rather than pretend, the sketch advertises only what it can carry out: the
Feature bit field declares "Delete all bonds on server (LE)" and leaves the
per-requesting-device bits clear. A spec-conformant client reads the Feature
first and will not ask for an operation that is not advertised.

## Key APIs

- `bluetooth.gattServer().onWritten(...)` — receive Control Point op codes; `write.connectionId` names the peripheral link but not its address
- `bluetooth.bondCount()` / `bluetooth.bond(i, bond)` / `bluetooth.deleteBond(bond)` / `bluetooth.deleteAllBonds()` — bond enumeration and removal

## Notes

- **Do the deletion from `loop()`, not from the write callback.** Bond removal waits for Bluedroid's asynchronous persistent store to settle, so it must not run inside event delivery.
- Op codes may carry an authorisation code as additional octets. This example accepts the bare op code only, which matches the cleared "with authorization" Feature bits.
- Bluetooth Classic bonds are a separate store (`bluetooth.classic().deleteAllBonds()`), so an LE-only op code must not touch them — which is why this example uses `deleteAllBonds()` on the BLE store alone.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Identifying the requesting peer | `onConnected()` / `onDisconnected()` give the peripheral link's `peerAddress` | **not available** — a peripheral link has no connection snapshot |
| Scope of op code 0x03 | that peer's bond only | every LE bond |
| Feature bit field | `0x000011` (per-requesting-device bits) | `0x000400` (delete all bonds on server, LE) |
| When the delete runs | from `onDisconnected()` | from `loop()`, three seconds after the write |

**Why:** the delete has to be scoped by peer address, and the address of an incoming link is exactly what EspBleBluedroid does not publish yet. Advertising a capability the server cannot implement would be worse than narrowing it, so the example narrows the Feature bits to match its behaviour.

**How to port:** replace the `onDisconnected()`-driven, address-scoped delete with a timer-driven `deleteAllBonds()`, and change the Feature value to match. Everything else — the service, the two characteristics, the op-code parsing in `onWritten()` — is identical to EspBle.

## Expected Serial output

```
Bond Management op code: 3
Deleting LE bonds in 3000 ms
Deleted 1 bond(s); remaining=0
```
