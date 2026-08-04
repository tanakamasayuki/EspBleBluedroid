# RuntimePasskeyClient

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 3, "Security"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

The **input side** of Passkey Entry (`KeyboardOnly`). It takes the 6 digits the peer displays **at runtime** and hands them over with `providePasskey()`. Its counterpart is any peer that **displays** a passkey generated per pairing: a smartphone, an EspBle board running `Security/RuntimePasskeyServer`, or a raw ESP-IDF peer. There is no `RuntimePasskeyServer` here — see [Security/README.md](../README.md).

[StaticPasskeyClient](../StaticPasskeyClient/) fixes the passkey in the sketch; this one follows what a real device does — the user reads it and types it in.

## Hardware

- 1 × original ESP32 running this sketch (central, input side)
- 1 × peer that displays a runtime passkey — a smartphone, an EspBle board running `Security/RuntimePasskeyServer`, or a raw ESP-IDF peer

## What it does

- Active-scans for the server's service UUID and connects to the first match
- `pairOnConnect` (on by default) starts pairing as soon as the connection comes up
- The stack asks for a passkey, and **pairing stops until it is answered**
- Sending `p` followed by 6 digits (e.g. `p481907`) calls `providePasskey()` and pairing resumes
- On success it discovers and reads a characteristic that requires `authenticatedRead`
- `c` deletes all bonds (only while disconnected)

## Key APIs

- `EspBleSecurityConfig::ioCapability = KeyboardOnly` — the side that types the passkey
- `bluetooth.providePasskey(passkey)` — hand the 6 digits to the waiting pairing
- `bluetooth.discoverCharacteristic(...)` / `bluetooth.readCharacteristic(...)` — access after pairing

## Notes

- **Answer within 30 seconds.** Past that the stack stops waiting and pairing fails. Do not combine this with anything that blocks `loop()` for long.
- `providePasskey()` is accepted **either before or after** the request arrives; a value supplied early is used by the next request.
- A wrong value fails pairing and `onSecurityChanged` reports `success=0`. **There is no mechanism that reports which part was wrong** — allowing that would let an attacker try the passkey one digit at a time.
- On later connections the bond applies and no pairing happens, so nothing is asked for. Send `c` on both sides to try again.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Passkey input side (this sketch) | `providePasskey()` from `KeyboardOnly` | identical |
| Response window | backend-defined | Bluedroid waits 30 s, then fails authentication |
| Passkey **display** side on this library | `Security/RuntimePasskeyServer` | **no counterpart** — the peripheral receives no `onPasskeyDisplayed()` ([Security/README.md](../README.md)) |
| Changing to `KeyboardOnly` after `end()` in the same boot | supported | needs a reboot when the previous run used a static or `DisplayOnly` passkey |

**Why:** the display side needs the stack to report the value it generated, and on
a peripheral-only device that event is not delivered yet (see
[docs/STATUS.ja.md](../../../docs/STATUS.ja.md)). The reboot requirement comes from
the Arduino-ESP32 BLE wrapper, which cannot clear an in-process passkey setting.

**How to port:** no change on this side. Run the display half on a phone, an
EspBle board, or a raw ESP-IDF peer.

## Expected Serial output

```
Send 'c' while disconnected to clear all bonds.
Connected id=1. Type p<passkey> (e.g. p123456) shown on the peer.
Passkey 481907 provided
Security established: encrypted=1 authenticated=1 bonded=1
Protected value: MITM protected value
```
