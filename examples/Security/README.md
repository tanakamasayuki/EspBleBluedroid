# Security examples

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 3, "Security"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../DIFFERENCES_FROM_ESPBLE.md)

Pairing, bonding, and attribute protection. Every example here is one half of a
pair — run a `…Client` on one board and a `…Server` (or a phone) on the other.

| Example | Role | Method |
|---|---|---|
| [JustWorksServer](JustWorksServer/) | Peripheral | Just Works + bonding, encrypted characteristic |
| [JustWorksClient](JustWorksClient/) | Central | Just Works + bonding, bond store inspection |
| [StaticPasskeyServer](StaticPasskeyServer/) | Peripheral | MITM, `DisplayOnly`, passkey fixed in the firmware |
| [StaticPasskeyClient](StaticPasskeyClient/) | Central | MITM, `KeyboardOnly`, the same fixed passkey |
| [RuntimePasskeyClient](RuntimePasskeyClient/) | Central | MITM, `KeyboardOnly`, passkey typed at run time |
| [NumericComparisonClient](NumericComparisonClient/) | Central | MITM, `DisplayYesNo`, confirm the six digits |

## Which side runs where

The **central** half is where BLE security is fully observable in this library:
`onSecurityChanged()`, `onPasskeyDisplayed()`, `onNumericComparison()`,
`providePasskey()`, and `confirmNumericComparison()` all work against a link this
device opened with `connect()`.

On a **peripheral-only** device EspBleBluedroid publishes no connection snapshot
yet, so those events are not delivered there (see
[docs/STATUS.ja.md](../../docs/STATUS.ja.md)). Pairing still completes and
everything it produces is observable — link encryption, the bond store, and
access to protected attributes — which is what the two server examples show.

That is why the set of examples here is asymmetric compared with EspBle:

| EspBle example | Here | Why |
|---|---|---|
| `Security/JustWorksServer` | [JustWorksServer](JustWorksServer/) | Ported; observes the bond store and the encrypted write instead of `onSecurityChanged()` |
| `Security/StaticPasskeyServer` | [StaticPasskeyServer](StaticPasskeyServer/) | Ported; the passkey is a compile-time constant, so nothing has to come from the stack |
| `Security/RuntimePasskeyServer` | **no counterpart** | The display side needs `onPasskeyDisplayed()` to learn the passkey the stack generated per pairing. Without that event the value can never be shown, so no working sketch can be written for this side yet |
| `Security/NumericComparisonServer` | **no counterpart** | The peripheral would have to receive the six digits and answer with `confirmNumericComparison()`. With no snapshot the request is not delivered and pairing is rejected, so the sketch could not work |
| — | [JustWorksClient](JustWorksClient/) | Added here: the central half of Just Works, which EspBle covers from its server example |

To exercise the two missing halves, run the client example here against a
smartphone, an EspBle board, or a raw ESP-IDF peer — which is how the library's
own peer tests (`tests/peer/runtime_passkey`, `tests/peer/numeric_comparison`)
verify them.

## Verification status

The **client** examples correspond to the library's own two-board peer tests
(`tests/peer/security_bond`, `security_passkey`, `runtime_passkey`,
`numeric_comparison`), which drive the public API as the central against a raw
ESP-IDF peer. The two **server** examples are not covered by a peer test yet:
attribute permissions and the bond store are library behaviour that is tested,
but the peripheral-side pairing flow in these two sketches has not been fixed by
an automated hardware test. Treat them as working examples of the API, not as
verified behaviour.

## Common configuration

```cpp
EspBleConfig config;
config.security.enabled = true;          // turn security on
config.security.bonding = true;          // store keys for encrypted reconnects
config.security.pairOnConnect = true;    // start pairing as soon as a link comes up
config.security.mitm = true;             // require MITM protection (passkey / comparison)
config.security.ioCapability = EspBleSecurityIoCapability::KeyboardOnly;
config.security.staticPasskeyEnabled = true;
config.security.staticPasskey = 438209;
```

| I/O capability | Method it selects | Sketch's job |
|---|---|---|
| `None` | Just Works | nothing |
| `DisplayOnly` | Passkey Entry (display side) | show the passkey |
| `KeyboardOnly` | Passkey Entry (input side) | supply it with `providePasskey()` |
| `DisplayYesNo` | Numeric Comparison | compare, then `confirmNumericComparison(accept)` |

## Notes that apply to all of them

- **Bluedroid waits up to 30 seconds** for `providePasskey()` or
  `confirmNumericComparison()`. An unanswered request fails authentication.
- **A rejected Numeric Comparison does not drop the link.** Bluedroid leaves the
  unencrypted BLE connection in place; call `disconnect()` if the application
  wants it closed.
- **`disconnect()` and `end()` while a passkey is pending cancel the wait** and
  return immediately.
- **Bond removal is asynchronous.** Call `deleteBond()` / `deleteAllBonds()`
  while disconnected; they wait for Bluedroid's persistent store to settle.
- **Bonds are separate per transport.** BLE bonds live in
  `bluetooth.bondCount()` / `bond()` / `deleteBond()`; Bluetooth Classic bonds
  live in `bluetooth.classic().bondCount()` and friends
  ([Classic/SppSecurity](../Classic/SppSecurity/)).
- **Changing a passkey configuration within one boot may need a reboot**, because
  the Arduino-ESP32 BLE wrapper cannot clear an in-process passkey setting.
