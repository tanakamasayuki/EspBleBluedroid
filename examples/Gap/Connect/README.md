# Connect

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 2, "GAP"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

Scans for a peripheral advertising a specific service UUID and connects to it as a central. Demonstrates the asynchronous connection model: `connect()` only accepts the request, and completion (or failure) arrives later as an event delivered from `bluetooth.update()`.

## Hardware

- 1 × original ESP32 running this sketch (central)
- 1 × BLE peripheral advertising the target service UUID — e.g. a second board running the [Gatt/Basics/Server](../../Gatt/Basics/Server/) example (change `TARGET_SERVICE_UUID` to match)

## What it does

- Starts an active scan and looks for `TARGET_SERVICE_UUID` in each result
- Stops the scan and requests a connection to the first match
- Prints connect, disconnect, and connection-failure events with the library connection ID
- Retries on the next scan result after a disconnect or failure

Edit `TARGET_SERVICE_UUID` at the top of the sketch to the UUID your peripheral advertises.

## Key APIs

- `scanResult.advertisesService(uuid)` — match either the 16-bit form (`"1812"`) or the 128-bit form
- `bluetooth.connect(scanResult)` — accepts the request and returns immediately; the connection itself runs on an internal task
  - `bluetooth.connect(scanResult, timeoutMilliseconds)` — the timeout is enforced from `update()` (default 10000 ms)
- `bluetooth.connect(address, EspBleAddressType, timeoutMilliseconds)` — connect from a saved address without scanning
- `bluetooth.onConnected(callback)` / `bluetooth.onDisconnected(callback)` — both carry the same stable library `connection.id`
- `bluetooth.onConnectionFailed(callback)` — asynchronous failure with `failure.detail`

## Notes

- **One central connection at a time.** A second `connect()` while a link is active is rejected with `InvalidState`; disconnect first.
- **A connection ID is only valid inside one `begin()`…`end()` lifecycle.** `end()` drops the active link and every undelivered event without calling `onDisconnected()`, and IDs may be reused after re-initialising.
- **`end()` during a connection attempt returns synchronously but may take up to about a second.** The wait is split into sub-second slices so the call still returns; the callbacks of the abandoned attempt are never delivered.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Simultaneous central connections | several | **one** |
| `connect()` model | asynchronous, result from `update()` | identical |
| `disconnect(id, reason)` overload | available | **not available** |

**Why:** the public API keeps a single central connection because the direct-GATTC migration is still in progress and one link is what the peer tests fix on real hardware (see [docs/STATUS.ja.md](../../../docs/STATUS.ja.md)). The reason-carrying `disconnect()` overload is absent because Arduino-ESP32 3.3.11's Bluedroid client does not pass a caller-supplied local reason down to link termination — the overload would silently ignore its argument. The HCI reason reported by the *peer* is still available in `onDisconnected()` as `connection.disconnectReason`.

**How to port:** connect to one peer at a time, and call `disconnect(id)` without a reason.

## Expected Serial output

```
Connected to 5a:b8:1e:0c:2f:71 (id=1)
Disconnected (id=1)
```
