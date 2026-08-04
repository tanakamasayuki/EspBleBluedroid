# JustWorksClient

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 3, "Security"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

Central-side counterpart of [JustWorksServer](../JustWorksServer/). It connects, pairs with **Just Works** (LE Secure Connections, no passkey) plus bonding, and then reads the characteristic that requires an encrypted link.

Just Works is the method that gets chosen when **neither side can display or enter a passkey** (`ioCapability = None` on both). It gives you encryption without MITM protection: a passive eavesdropper is locked out, an active man-in-the-middle during pairing is not.

## Hardware

- 1 × original ESP32 running this sketch (central)
- 1 × BLE peripheral with an encrypted characteristic — the [JustWorksServer](../JustWorksServer/) example on a second board, or a phone app

## What it does

- Active-scans for the server's service UUID and connects to the first match
- `pairOnConnect` starts pairing as soon as the link comes up; no user interaction happens
- Prints the security result and the first stored bond
- After security is established, discovers and reads the encrypted characteristic
- `c` deletes all bonds while disconnected and prints the remaining count

## Key APIs

- `EspBleSecurityConfig` — `enabled`, `bonding`, `pairOnConnect`; `ioCapability` stays `None`
- `bluetooth.onSecurityChanged(callback)` — delivered from `update()`; `success` plus the connection security snapshot (`encrypted`, `authenticated`, `bonded`, `encryptionKeySize`)
- `bluetooth.bondCount()` / `bluetooth.bond(index, out)` / `bluetooth.deleteAllBonds()` — the BLE bond store
- `bluetooth.requestSecurity(connectionId)` — the alternative to `pairOnConnect`: start pairing explicitly ([StaticPasskeyClient](../StaticPasskeyClient/) uses this form)

## Notes

- **`authenticated` stays 0 on success.** That is Just Works, not a failure. Require `authenticatedRead` / `authenticatedWrite` on an attribute and this method cannot reach it — use a passkey method instead ([StaticPasskeyClient](../StaticPasskeyClient/), [RuntimePasskeyClient](../RuntimePasskeyClient/)).
- **A bonded reconnect encrypts without pairing again.** The stored keys are used, so `onSecurityChanged()` reports success with no user-visible step. Delete the bond on **both** sides to pair from scratch.
- **Delete bonds while disconnected.** `deleteBond()` / `deleteAllBonds()` wait for Bluedroid's asynchronous persistent-store update before returning.
- BLE bonds and Bluetooth Classic bonds are separate stores; the Classic side is `bluetooth.classic().bondCount()` and friends.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Example itself | not present (EspBle covers Just Works from its server example) | provided here, because the central is the side where security events are delivered |
| `onSecurityChanged()` on a central link | delivered | identical |
| Bond store API | `bondCount()` / `bond()` / `deleteBond()` / `deleteAllBonds()` | identical, plus a separate `classic()` bond store |

**Why:** on this library the peripheral half of a pairing receives no security events yet, so the central-side sketch is the one that can show the whole sequence (see [Security/README.md](../README.md)).

**How to port:** the sketch body maps one-to-one onto EspBle; only the class and instance names change.

## Expected Serial output

```
Send 'c' while disconnected to clear all bonds.
Connected: 1
Security: success=1 encrypted=1 bonded=1 key=16
Stored bond: d0:cf:13:58:fd:95
Encrypted value: encrypted value
```
