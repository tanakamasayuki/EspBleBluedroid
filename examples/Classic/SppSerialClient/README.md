# SppSerialClient

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [Bluetooth Classic beginner guide (Japanese)](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) — SPP
> EspBle: no counterpart — Bluetooth Classic only ([DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md))

The outgoing half of the `Stream` bridge: type a Classic address to connect, then everything typed afterwards travels over SPP as if it were a serial cable.

The same `EspBluedroidSppSerial` wrapper works for both roles; only who initiates the connection differs.

## Hardware

- 1 × original ESP32 running this sketch (SPP client)
- 1 × SPP server — a second board running [SppSerialServer](../SppSerialServer/), or any Classic serial port

## What it does

- Prints a prompt and waits for a Classic address on `Serial` **while disconnected**
- Calls `classic().spp().connect(address)` with that address
- Once connected, forwards `Serial` → SPP and SPP → `Serial`
- On a disconnect or a failed attempt, prints why and asks for an address again

## The one structural trick

The sketch uses `sppSerial.connected()` to decide **what a typed line means**:

```cpp
if (!sppSerial.connected() && Serial.available() > 0) { /* it is an address */ }
else { /* it is data to send */ }
```

Without that split, the address you type to connect would be sent as payload — or worse, payload would be parsed as an address.

## Key APIs

- `bluetooth.classic().spp().connect(address)` — asynchronous; the result arrives through `onConnected()` / `onConnectionFailed()`
- `EspBluedroidSppSerial` — `connected()`, `available()`, `read()`, `write()`, `sessionId()`, `availableForWrite()`
- `onDisconnected()` / `onConnectionFailed()` — where this sketch re-prompts for an address

## Notes

- **The wrapper follows the new session after a reconnect.** No rebinding, and no stale session ID.
- **`connect()` needs the address, not a scan result.** Use [Inquiry](../Inquiry/) to find it.
- A rejected request (bad address, session already active) is reported synchronously by the `false` return plus `lastErrorDetail()`; a peer that never answers ends as a timeout event.
- One session at a time across both roles, so this sketch cannot connect out while an incoming session is active.

## Expected Serial output

```
Enter a Classic address such as 01:23:45:67:89:ab
connected: id=1 peer=d0:cf:13:58:fd:95
hello from the server
disconnected: id=1
Enter the peer Classic address to reconnect
```
