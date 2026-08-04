# NumericComparisonClient

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 3, "Security"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

The central side of **Numeric Comparison** pairing. Its counterpart is any `DisplayYesNo` peer — a smartphone, an EspBle board running `Security/NumericComparisonServer`, or a raw ESP-IDF peer (there is no server example here; see [Security/README.md](../README.md)). The configuration is **exactly the same on both sides** (`DisplayYesNo` plus MITM required) — both sides declaring the same thing is what makes this method get chosen.

## Hardware

- 1 × original ESP32 running this sketch (central)
- 1 × `DisplayYesNo` peer — a smartphone, an EspBle board running `Security/NumericComparisonServer`, or a raw ESP-IDF peer

Keep both serial monitors visible at once.

## What it does

- Active-scans for the server's service UUID and connects to the first match
- `pairOnConnect` (on by default) starts pairing as soon as the connection comes up
- The 6 digits to compare arrive at `onNumericComparison` — they should equal what the server shows
- `y` accepts, `n` rejects. **Pairing is stopped until it is answered**
- Once both sides accept, it discovers and reads a characteristic requiring `authenticatedRead`
- `c` deletes all bonds (only while disconnected)

## Key APIs

- `EspBleSecurityConfig::ioCapability = DisplayYesNo`
- `bluetooth.onNumericComparison(cb)` / `bluetooth.confirmNumericComparison(accept)`
- `bluetooth.discoverCharacteristic(...)` / `bluetooth.readCharacteristic(...)` — access after pairing

## Notes

- **A rejection from either side fails the pairing.** One `n` ends it for both.
- **Answer within 30 seconds**, or the stack stops waiting.
- On later connections the bond applies and no pairing happens, so nothing is asked. Send `c` on both sides to try again.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Comparison side (this sketch) | `onNumericComparison()` + `confirmNumericComparison()` | identical |
| Response window | backend-defined | Bluedroid waits 30 s, then fails authentication |
| After a rejection | link handling is up to the backend | the **unencrypted BLE link stays up**; call `disconnect()` if it should close |
| Peripheral half on this library | `Security/NumericComparisonServer` | **no counterpart** ([Security/README.md](../README.md)) |

**Why:** the peripheral would have to receive the six digits and answer, and
EspBleBluedroid delivers no such request without a connection snapshot for the
incoming link (see [docs/STATUS.ja.md](../../../docs/STATUS.ja.md)). Bluedroid also
does not tear the link down when the application answers `false`, so closing it is
the application's decision.

**How to port:** no change on this side, and add an explicit `disconnect()` after a
rejection if the sketch should not stay connected.

## Expected Serial output

```
Send 'c' while disconnected to clear all bonds.
Does the peer show 052913? Send 'y' to accept, 'n' to reject.
Answer accept: sent
Security established: encrypted=1 authenticated=1 bonded=1
Protected value: MITM protected value
```
