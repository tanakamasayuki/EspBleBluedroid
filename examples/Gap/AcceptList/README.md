# AcceptList

> 日本語版: [README.ja.md](README.ja.md)

A peripheral-side example that restricts who may connect.

BLE has no application callback for inspecting and approving a connection request. The Filter Accept List (formerly the white list) lets the controller compare the requester's address and discard unlisted requests before they reach the application.

The same list applies to central scanning when `EspBleScanConfig::acceptListOnly`
is true; the controller then discards advertising from unlisted addresses.

## Hardware

- 1 × original ESP32 running this sketch
- A central that attempts to connect (a second ESP32 or a phone)

Replace `ALLOWED_CENTRAL` with the allowed central's identity address, and select the matching `Public` or `Random` address type. With the placeholder unchanged, normally no central can connect.

## Behavior

- Adds the allowed identity address to the accept list
- Advertises with `ConnectionFromAcceptList`, so the controller drops unlisted connection requests
- Sending `o` over Serial switches to `Any` and permits every central
- Sending `r` restores the accept-list restriction

The current public API does not yet expose peripheral GATT servers or peripheral-side connection callbacks, so verify this example from the central's connection result.

## Main APIs

- `bluetooth.addToAcceptList(address, addressType)` — up to 8 entries; a duplicate succeeds without consuming another slot
- `bluetooth.removeFromAcceptList(address, addressType)`
- `bluetooth.clearAcceptList()`
- `bluetooth.acceptListCount()` / `bluetooth.acceptListEntry(index, entry)`
- `bluetooth.advertising().setFilterPolicy(policy)`
- `EspBleScanConfig::acceptListOnly` — apply the same list while scanning

The four policies are:

| Policy | Scan requests | Connection requests |
|---|---|---|
| `Any` | Allow all | Allow all |
| `ScanRequestFromAcceptList` | Listed peers only | Allow all |
| `ConnectionFromAcceptList` | Allow all | Listed peers only |
| `Both` | Listed peers only | Listed peers only |

## Notes

- The policy and list are copied to the controller on the next `advertising.start()`. To change them while advertising, call `stop()`, change them, then call `start()`.
- A restrictive policy with an empty list discards every applicable request.
- A rejected central receives no rejection packet; it observes a connection failure or timeout.
- For a peer using RPA, list its bonded identity address with the correct address type. Do not permanently list a temporary observed RPA.
- Connection filtering does not replace attribute encryption. Configure BLE Security as well when values need protection.

## Expected Serial output

```text
Restricted advertising. Only aa:bb:cc:dd:ee:ff may connect. Send o/r to change policy.
Policy: open (accept list has 1 entries)
Policy: restricted (accept list has 1 entries)
```
