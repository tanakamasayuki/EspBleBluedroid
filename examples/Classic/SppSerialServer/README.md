# SppSerialServer

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [Bluetooth Classic beginner guide (Japanese)](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) — SPP
> EspBle: no counterpart — Bluetooth Classic only ([DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md))

Bridges an incoming SPP session and the board's USB `Serial` port, using `EspBluedroidSppSerial` — the Arduino `Stream` view of SPP.

This is the shape most "wireless serial cable" projects want: whatever arrives over Bluetooth is written to `Serial`, and whatever you type in the Serial Monitor goes out over Bluetooth.

```cpp
EspBleBluedroid bluetooth;
EspBluedroidSppSerial sppSerial(bluetooth);
```

## Hardware

- 1 × original ESP32 running this sketch (SPP server)
- 1 × SPP client — a second board running [SppSerialClient](../SppSerialClient/), a phone terminal app, or a PC Bluetooth serial port

## What it does

- Starts an SPP server named `EspBleBluedroid Serial`
- Prints connect and disconnect events
- Copies every byte from `sppSerial` to `Serial`, and from `Serial` to `sppSerial` while connected

## Key APIs

- `EspBluedroidSppSerial sppSerial(bluetooth)` — construct once, at global scope, next to the `EspBleBluedroid` instance
- `sppSerial.connected()` / `explicit operator bool()` / `sessionId()`
- `available()` / `peek()` / `read()` / `write()` / `print()` / `println()` / `readBytes()` / `flush()` — the standard `Stream` and `Print` API
- `availableForWrite()` — remaining room in the fixed write queue, in bytes

## Why no session ID is needed

The wrapper **follows the current single active session by itself**. It does not own the stack or the session, so:

- `connected()` becomes false on disconnect and true again when the next session arrives — including a session from the other role
- after a reconnect it targets the new session with no application-side rebinding
- writes are split into 990-byte chunks to match the SPP write limit

The one rule: **the wrapper must not outlive the `EspBleBluedroid` instance** it was constructed from.

## Notes

- **Keep calling `bluetooth.update()`.** The wrapper reads from the same receive ring the stack fills, but connect / disconnect events and write completion still flow through `update()`.
- `write()` while disconnected fails; guard with `connected()` as this sketch does.
- Mixing `sppSerial.read()` with `onData()` packet events consumes the same bytes twice — choose one style.
- For the explicit session-ID API (and echo, queue diagnostics, security), see [SppServer](../SppServer/).

## Expected Serial output

```
connected: id=1 peer=20:32:c6:1e:9d:4a
hello from the phone
disconnected: id=1
```
