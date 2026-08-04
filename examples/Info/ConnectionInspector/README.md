# ConnectionInspector

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 2, "GAP"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

Interactive diagnostic tool. It lists nearby connectable devices with index numbers, connects to the one you pick, and dumps the full connection snapshot: connection ID, backend handle, peer address and type, local role, negotiated MTU (and the resulting notification payload limit), and the security state (encrypted / authenticated / bonded / key size). It can also dump the bond store and the library's diagnostic counters.

## Hardware

- 1 × original ESP32 running this sketch (central)
- Nearby BLE peripherals to inspect (any advertising device)

## What it does

- Scans and lists up to 10 unique connectable devices as `[index] address rssi name`
- `0`–`9` connects to the listed device with that index and prints its connection snapshot
- `s` clears the list and rescans; `d` disconnects the current connection; `b` dumps the bond store; `q` prints the diagnostic counters
- Security is disabled here, so peripherals that require encryption still accept the connection and show their link info, but reject attribute access

## Key APIs

- `EspBleConnection` — `id`, `handle`, `peerAddress`, `peerAddressType`, `localRole`, `mtu`, `maximumNotificationPayload()`, `encrypted`, `authenticated`, `bonded`, `encryptionKeySize`
- `bluetooth.connect(scanResult)` / `bluetooth.disconnect(connectionId)` / `bluetooth.onConnectionFailed(callback)`
- `bluetooth.bondCount()` / `bluetooth.bond(index, out)` — snapshot access to the bond store
- `bluetooth.connectionCount()`, `bluetooth.droppedEventCount()`, `bluetooth.scanner().droppedResultCount()`

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Connections listed by `connectionCount()` / `connection()` | every link, incoming ones included | **central links only** — links this device opened with `connect()` |
| Simultaneous connections | several, so the list can hold more than one | one at a time |
| `connection.localRole` | `Central` or `Peripheral` | always `Central` |
| `droppedEventCount()` | event-queue overflow counter | identical |

**Why:** peripheral connection snapshots are not published yet (see [docs/STATUS.ja.md](../../../docs/STATUS.ja.md)), so this inspector reports the link it opened and nothing else. To inspect an incoming link, run the inspector on the other board and connect from there.

**How to port:** no code change; just do not expect more than one entry, and do not expect a `Peripheral` role.

## Expected Serial output

```
Commands: 0-9 connect to listed device, s rescan, d disconnect, b bonds, q counters
SCAN restart success=1 - send the list number to connect
[0] 5a:b8:1e:0c:2f:71 rssi=-52 name=EspBleBluedroid Keyboard
CONNECT [0] 5a:b8:1e:0c:2f:71 accepted=1
CONNECTION id=1 handle=0 peer=5a:b8:1e:0c:2f:71(type=0) role=Central
  mtu=255 maxNotificationPayload=252
  encrypted=0 authenticated=0 bonded=0 keySize=0
```
