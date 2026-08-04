# StaticPasskeyServer

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 3, "Security"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

A GATT server (peripheral) requiring MITM-authenticated pairing with a static 6-digit passkey. This board is the display side (`DisplayOnly`): it prints the passkey and the connecting central types it. Pairs with the [StaticPasskeyClient](../StaticPasskeyClient/) example (the passkey-input side).

## Hardware

- 1 × original ESP32 running this sketch (peripheral / GATT server, passkey display side)
- 1 × BLE central with keyboard input: the [StaticPasskeyClient](../StaticPasskeyClient/) example or a smartphone app

## What it does

- Registers a characteristic with `authenticatedRead` / `authenticatedWrite` permissions before `begin()`
- Enables security with `mitm`, `ioCapability = DisplayOnly`, and the static passkey `438209`
- Prints the passkey at startup — it is a compile-time constant, so no callback is needed to learn it
- Prints the stored bond as soon as the bond count changes, which is the peripheral-side evidence that MITM-authenticated pairing completed

## Key APIs

- `EspBleGattCharacteristicConfig::authenticatedRead` / `authenticatedWrite` — require a MITM-authenticated link
- `EspBleSecurityConfig` — `mitm`, `ioCapability` (`DisplayOnly` / `KeyboardOnly`), `staticPasskeyEnabled`, `staticPasskey`
- `bluetooth.bondCount()` / `bluetooth.bond(index, out)` — the device-wide bond store, readable without a connection snapshot

## Notes

- The characteristic requires an authenticated link, so Just Works pairing cannot access it.
- The passkey is a fixed example value. Production devices should provision a unique passkey per device instead of hard-coding a shared one.
- **The static form is the one that works on this side.** A runtime-generated passkey would have to be reported by the stack, and a peripheral-only device receives no such event here — which is why there is no `RuntimePasskeyServer` counterpart (see [Security/README.md](../README.md)).
- **Changing the passkey configuration within one boot needs a restart.** The Arduino-ESP32 BLE wrapper cannot clear an in-process passkey setting, so going from a static/`DisplayOnly` passkey to `KeyboardOnly` runtime input after `end()` requires a reboot. Re-initialising with the same configuration is unaffected.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| `onPasskeyDisplayed()` on a peripheral-only device | delivered when pairing starts | **not delivered** — print the static constant instead |
| `onSecurityChanged()` on a peripheral-only device | delivered | **not delivered** — watch `bondCount()` |
| `onDisconnected()` to restart advertising | delivered | **not delivered** |
| `authenticatedRead` / `authenticatedWrite` | enforced by ATT | identical |
| Changing passkey I/O capability after `end()` | supported | needs a reboot when moving to `KeyboardOnly` runtime input |

**Why:** the same reason as [JustWorksServer](../JustWorksServer/) — BLE security events attach to a connection snapshot, and EspBleBluedroid publishes snapshots only for links this device opened with `connect()` (see [docs/STATUS.ja.md](../../../docs/STATUS.ja.md)). With a static passkey nothing is lost, because the value the user must type is already known to the sketch.

**How to port:** move the passkey print out of `onPasskeyDisplayed()` and into `setup()`, and replace `onSecurityChanged()` with a `bondCount()` watch. The characteristic permissions and the `EspBleSecurityConfig` fields are used exactly as in EspBle.

## Expected Serial output

```
Enter passkey 438209 on the peer.
Advertising. Stored bonds: 0
Bonded with 5a:b8:1e:0c:2f:71 (total 1)
```
