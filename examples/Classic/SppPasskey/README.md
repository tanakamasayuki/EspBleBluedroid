# SppPasskey

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [Bluetooth Classic beginner guide (Japanese)](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) — Security
> EspBle: no counterpart — Bluetooth Classic only ([DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md))

An authenticated, encrypted SPP server using Classic SSP **Passkey Entry**. It defaults to `KeyboardOnly`: the peer displays six digits, you type them into the Serial Monitor.

Where [SppSecurity](../SppSecurity/) has both sides compare a number, Passkey Entry has one side **display** and the other **enter** it. This sketch handles both directions — flip `ioCapability` to `DisplayOnly` and the ESP32 becomes the display side instead.

## Hardware

- 1 × original ESP32 running this sketch (SPP server, passkey **input** side by default)
- 1 × peer that displays a passkey — a phone, or a second board configured as `DisplayOnly`

## What it does

- Enables Classic security with `ioCapability = KeyboardOnly` before `begin()`
- Starts an SPP server requiring `AuthenticatedEncrypted`
- On `onPasskeyRequested()`, remembers the peer address and asks for the six digits
- Sends the typed value with `providePasskey(peerAddress, passkey)`
- Also implements `onPasskeyDisplayed()`, so the same sketch works when configured as `DisplayOnly`
- Prints the authentication result and the session's `authenticated` / `encrypted` flags, then echoes data

## Key APIs

- `EspBleConfig::classicSecurity.ioCapability` — `KeyboardOnly` (enter) or `DisplayOnly` (show)
- `bluetooth.classic().onPasskeyRequested(cb)` — `EspBluedroidClassicPasskeyRequested::peerAddress`
- `bluetooth.classic().onPasskeyDisplayed(cb)` — `EspBluedroidClassicPasskeyDisplayed` with `peerAddress` and `passkey`
- `bluetooth.classic().providePasskey(peerAddress, passkey)` — answer the one pending request; `true` means the reply was accepted, not that pairing succeeded
- `bluetooth.classic().onSecurityChanged(cb)` — the actual pairing outcome
- `bluetooth.classic().bondCount()` / `deleteAllBonds()` — the Classic link-key store

## Notes

- **`providePasskey()` returning true only means the reply was accepted.** Whether pairing succeeded comes from `onSecurityChanged()`.
- **A late answer is rejected.** The request expires after `responseTimeoutMilliseconds` (30 s by default); typing afterwards fails, and the peer may then retry immediately.
- **`disconnect()` and `end()` while a passkey is pending cancel the wait** and return promptly, rather than blocking for the full timeout.
- **Switching I/O capability within one boot can need a reboot.** Bluedroid keeps the pairing configuration process-wide; changing from a display-side setup to `KeyboardOnly` after `end()` may require a restart.
- Passkeys are per-pairing values shown by the peer — do not hard-code one here. The static-passkey pattern belongs to BLE ([Security/StaticPasskeyServer](../../Security/StaticPasskeyServer/)).

## Expected Serial output

```
Enter the six-digit passkey shown by 20:32:c6:1e:9d:4a:
Passkey reply accepted: 1
Security succeeded for 20:32:c6:1e:9d:4a (status=0)
Secure SPP connected: authenticated=1 encrypted=1
```
