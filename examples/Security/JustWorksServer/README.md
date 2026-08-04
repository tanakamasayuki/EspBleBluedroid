# JustWorksServer

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 3, "Security"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

A GATT server (peripheral) whose characteristic requires an encrypted link. Pairing uses Just Works (no passkey, LE Secure Connections) with bonding, started automatically on connection. Pairs with any BLE central (smartphone app such as nRF Connect, or another board).

## Hardware

- 1 × original ESP32 running this sketch (peripheral / GATT server)
- 1 × BLE central that supports pairing (smartphone app or a second board)

## What it does

- Registers a characteristic with `encryptedRead` / `encryptedWrite` permissions before `begin()`
- Enables security with bonding and `pairOnConnect`, so pairing starts as soon as a central connects
- Prints any value written to the encrypted characteristic — a successful write **is** the evidence that the ATT layer accepted an encrypted link
- Prints the stored bond as soon as the bond count changes
- Deletes all bonds on Serial command `c` (only allowed while disconnected) and prints the remaining count

## Key APIs

- `EspBleGattCharacteristicConfig::encryptedRead` / `encryptedWrite` — enforce an encrypted link at the ATT layer
- `EspBleSecurityConfig` — `enabled`, `bonding`, `pairOnConnect`
- `bluetooth.bondCount()` / `bluetooth.bond(index, out)` — the device-wide bond store, readable without a connection snapshot
- `bluetooth.deleteAllBonds()` — bond store management
- `gattServer.onWritten(callback)` — the write that only an encrypted link can perform

## Notes

- Just Works pairing yields `encrypted=1` but `authenticated=0` (no MITM protection). Reading the characteristic before encryption fails with insufficient-encryption, which prompts the OS to pair.
- **Attribute permissions are enforced by the ATT layer, not by this sketch.** That part works regardless of which side observes the pairing.
- Bond removal waits for Bluedroid's asynchronous persistent-store update before returning, so call it while disconnected.
- Pair this with [Security/JustWorksClient](../JustWorksClient/) to see the same pairing from the central side, where the security events *are* delivered.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| `onSecurityChanged()` on a peripheral-only device | delivered | **not delivered** — observe the write and the bond store instead |
| `onPasskeyDisplayed()` / `onNumericComparison()` on a peripheral | delivered | **not delivered** |
| `onDisconnected()` to restart advertising | delivered | **not delivered** — Bluedroid keeps this advertising set restartable from a command or timer |
| Encrypted / authenticated attribute permissions | enforced by ATT | identical |
| `bondCount()` / `bond()` / `deleteAllBonds()` | device-wide | identical |
| `Security/RuntimePasskeyServer`, `Security/NumericComparisonServer` | provided | **no counterpart** (see [Security/README.md](../README.md)) |

**Why:** BLE security events are raised by the backend against the active connection snapshot, and EspBleBluedroid publishes snapshots for links this device opened with `connect()` only (see [docs/STATUS.ja.md](../../../docs/STATUS.ja.md)). On a device that is purely a peripheral there is no snapshot to attach the event to, so it is not delivered. Pairing itself still completes, and everything it produces — encryption on the link, an entry in the bond store, access to protected attributes — is observable.

**How to port:** drop the `onSecurityChanged()` handler and read the outcome instead: watch `bondCount()`, and treat a successful read/write of the protected characteristic as proof of an encrypted link. Run the central half on a phone, on [Security/JustWorksClient](../JustWorksClient/), or on an EspBle board if you want the event trace.

## Expected Serial output

```
Advertising. Stored bonds: 0
Send 'c' while disconnected to clear all bonds.
Bonded with 5a:b8:1e:0c:2f:71 (total 1)
Encrypted write: hello
```
