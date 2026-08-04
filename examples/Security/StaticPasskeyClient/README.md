# StaticPasskeyClient

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 3, "Security"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

Central-side counterpart of [StaticPasskeyServer](../StaticPasskeyServer/): the passkey-input side (`KeyboardOnly`) of MITM-authenticated pairing. After pairing succeeds it discovers and reads the characteristic that requires an authenticated link.

Because the passkey is fixed in the sketch, nothing is typed at runtime. For the form where **the user actually types it**, see [RuntimePasskeyClient](../RuntimePasskeyClient/).

## Hardware

- 1 × original ESP32 running this sketch (central, passkey input side)
- 1 × original ESP32 running the [StaticPasskeyServer](../StaticPasskeyServer/) example

## What it does

- Active-scans for the server's service UUID and connects to the first match
- Starts pairing explicitly with `requestSecurity()` on connection
- On security success, discovers the MITM-protected characteristic and reads it
- Prints the security result and the protected value
- Deletes all bonds on Serial command `c` (only allowed while disconnected) and prints the remaining count

## Key APIs

- `EspBleSecurityConfig::ioCapability = KeyboardOnly` — this side "types" the passkey
- `config.security.staticPasskeyEnabled` / `staticPasskey` — preconfigured passkey passed to the stack
- `bluetooth.requestSecurity(connectionId)` — explicit pairing start; completion arrives via `onSecurityChanged()`
- `bluetooth.discoverCharacteristic(...)` / `bluetooth.readCharacteristic(...)` — access the characteristic after pairing

## Notes

- The static passkey is handed to the stack up front, so `STATIC_PASSKEY` here must match the value the server displays. **It is compiled into the sketch and is therefore no secret from anyone who can read the source.** For production, prefer the runtime form ([RuntimePasskeyClient](../RuntimePasskeyClient/)).
- `authenticatedRead` characteristics are only readable after MITM pairing completes; a read before that fails with an ATT security error.

## Differences from EspBle

Usage is identical on this side; only the peer half differs. See
[Security/README.md](../README.md) for why the peripheral-side examples are
asymmetric, and [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)
for the library-wide list.

## Expected Serial output

```
Send 'c' while disconnected to clear all bonds.
Security established: encrypted=1 authenticated=1 bonded=1
Protected value: MITM protected value
```
