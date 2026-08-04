# SppServer

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [Bluetooth Classic beginner guide (Japanese)](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) — SPP
> EspBle: no counterpart — Bluetooth Classic only ([DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md))

Starts an **unauthenticated Serial Port Profile server** and echoes every packet it receives.

SPP is the Classic answer to "just send me some bytes": RFCOMM gives you a two-way byte stream, and a phone or PC sees it as a serial port. There is no GATT database and no attribute permissions — which is why the security examples ([SppSecurity](../SppSecurity/), [SppPasskey](../SppPasskey/)) exist as separate steps.

For the `Stream`-style version of the same server, see [SppSerialServer](../SppSerialServer/).

## Hardware

- 1 × original ESP32 running this sketch (SPP server, the side that waits)
- 1 × SPP client — a second board running [SppClient](../SppClient/), a phone terminal app that connects to a Classic serial port, or a PC's Bluetooth serial port

## What it does

- Checks `capabilities().classicSpp` before `begin()`
- Starts an SPP server with the service name `EspBleBluedroid SPP`, which is what the peer sees in SDP
- Prints connect and disconnect events with the session ID and peer address
- Echoes each received packet back with `write(sessionId, value)`
- Reports a rejected write with `lastErrorName()`

## Key APIs

- `bluetooth.classic().spp().startServer(config)` — `EspBluedroidSppServerConfig::serviceName`, `channel` (0 = let the stack pick the RFCOMM channel), `security`
- `onServerStarted()` / `onConnected()` / `onDisconnected()` / `onData()` — all delivered from `bluetooth.update()`
- `write(sessionId, value)` / `write(sessionId, data, length)` — 1–990 bytes per call
- `disconnect(sessionId)` / `session(sessionId, out)` / `sessionCount()`
- `pendingWriteCount()` / `droppedWriteCount()` / `droppedReceiveByteCount()` / `droppedEventCount()` — bounded-resource diagnostics
- `available(sessionId)` / `peek(sessionId)` / `read(sessionId)` — the receive ring, readable without waiting for an `onData()` event

## Bounded resources

| Resource | Limit | Overflow visible as |
|---|---|---|
| Sessions | 1 pending or active | `startServer()` accepts one peer at a time |
| Write queue | 8 entries | `write()` returns false; `droppedWriteCount()` increases |
| Write size | 1–990 bytes | `write()` returns false with `InvalidArgument` |
| Receive ring | 2048 bytes | oldest bytes are kept; excess counted by `droppedReceiveByteCount()` |

## Notes

- **SPP data is binary-safe.** The event owns a copied `String`, so embedded NUL bytes survive: use `value.length()` and indexing rather than C-string functions.
- **Callbacks come from `update()`, not from the Bluedroid callback.** Echoing from inside `onData()` is therefore safe — the write is queued, not sent from a stack context.
- **`onWriteCompleted()` tells you when the backend finished a write**, with the session ID, byte count, and error detail. A write still sitting in the queue at disconnect time produces no completion event.
- **Two ways to read the same bytes.** `onData()` gives packet events; `available()` / `read()` give a byte stream from the ring. Pick one style per sketch to avoid consuming the same data twice.
- The peer sees whatever `serviceName` you set; make it recognisable when several boards are in the room.

## Expected Serial output

```
SPP server started
connected: id=1 peer=20:32:c6:1e:9d:4a
received 5 bytes
received 12 bytes
disconnected: id=1
```
