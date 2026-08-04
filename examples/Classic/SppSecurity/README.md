# SppSecurity

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [Bluetooth Classic beginner guide (Japanese)](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) — Security
> EspBle: no counterpart — Bluetooth Classic only ([DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md))

An SPP server that **requires authentication and link encryption**, paired with Secure Simple Pairing (SSP) **Numeric Comparison**: both devices show the same six digits, and a human confirms they match.

Classic security sits in two places on purpose:

| Where | What it decides |
|---|---|
| `EspBleConfig::classicSecurity` | How this device pairs — I/O capability, response timeout. Set once for the whole device |
| `EspBluedroidSppServerConfig::security` | What this SPP service demands — none, authenticated, or authenticated + encrypted |

Keeping them apart means a future Classic profile can share the pairing UI without inheriting SPP's policy.

## Hardware

- 1 × original ESP32 running this sketch (secure SPP server)
- 1 × `DisplayYesNo` peer — a phone (its pairing dialog shows the six digits), or a second board with the same configuration

Keep both screens visible: the whole point is comparing the values.

## What it does

- Enables Classic security with `ioCapability = DisplayYesNo` before `begin()`
- Starts an SPP server whose `security` is `AuthenticatedEncrypted`
- Prints the six-digit comparison value with the peer address, and waits for `y` / `n` on Serial
- Answers with `confirmNumericComparison(peerAddress, accept)`
- Prints the authentication result, then the session's `authenticated` / `encrypted` flags
- Echoes received data over the now-encrypted session

## Key APIs

- `EspBleConfig::classicSecurity` — `enabled`, `ioCapability`, `responseTimeoutMilliseconds` (default 30000)
- `EspBluedroidSppSecurity` — `None`, `Authenticate`, `AuthenticatedEncrypted`
- `bluetooth.classic().onNumericComparisonRequested(cb)` — `EspBluedroidClassicNumericComparison` with `peerAddress` and `value`
- `bluetooth.classic().confirmNumericComparison(peerAddress, accept)`
- `bluetooth.classic().onSecurityChanged(cb)` — `success` plus the backend `status` code
- `EspBluedroidSppSession::authenticated` / `encrypted`
- `bluetooth.classic().bondCount()` / `bond(i, out)` / `deleteBond(bond)` / `deleteAllBonds()` — the **Classic** link-key store

## The four I/O capabilities

| `ioCapability` | Method | This sketch's job |
|---|---|---|
| `None` | Just Works | nothing |
| `DisplayOnly` | Passkey Entry (display) | show the value ([SppPasskey](../SppPasskey/)) |
| `KeyboardOnly` | Passkey Entry (input) | `providePasskey()` ([SppPasskey](../SppPasskey/)) |
| `DisplayYesNo` | Numeric Comparison | compare, then `confirmNumericComparison()` |

## Notes

- **Answer within `responseTimeoutMilliseconds`** (30 s by default). An unanswered request is rejected and authentication fails.
- **Requests carry the peer address**, and the answer takes it back — so the reply cannot be attached to the wrong pairing.
- **Classic link keys and BLE bonds are separate stores.** `classic().deleteAllBonds()` does not touch BLE bonds, and `bluetooth.deleteAllBonds()` does not touch Classic link keys.
- **A stored link key means no dialog next time.** Reconnecting a bonded peer becomes secure with no confirmation UI. Delete the key on both sides to see the comparison again.
- After an authentication failure the peer can retry; the example keeps the server running.

## Expected Serial output

```
Compare 419203 with 20:32:c6:1e:9d:4a, then enter y or n
Classic authentication succeeded: peer=20:32:c6:1e:9d:4a status=0
secure SPP connected: authenticated=1 encrypted=1
```
