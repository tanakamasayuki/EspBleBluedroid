# SppClient

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [Bluetooth Classic beginner guide (Japanese)](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) — SPP
> EspBle: no counterpart — Bluetooth Classic only ([DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md))

The connecting side of SPP. Type a Classic address in the Serial Monitor and the sketch connects to that device's serial port and sends `hello`.

Unlike BLE, there is no scan result to hand to `connect()`: **Classic connects by address**. Get the address from [Inquiry](../Inquiry/), from the peer's own log, or from your phone's Bluetooth settings.

## Hardware

- 1 × original ESP32 running this sketch (SPP client, the side that connects)
- 1 × SPP server — a second board running [SppServer](../SppServer/), or any device offering a Classic serial port

## What it does

- Initialises the stack and prints a prompt
- Reads a canonical address (`01:23:45:67:89:ab`) from Serial and calls `connect()`
- On `onConnected()`, sends `hello` and prints the session ID and peer address
- Prints the byte count of each received packet
- Prints disconnects, and connection failures with the peer address and error detail

## Key APIs

- `bluetooth.classic().spp().connect(address, timeoutMilliseconds, security)` — accepts the request and returns immediately; SDP discovery and the RFCOMM connection finish later
- `onConnected()` / `onDisconnected()` / `onData()` / `onConnectionFailed()` — delivered from `bluetooth.update()`
- `EspBluedroidSppSession` — `id`, `peerAddress`, `incoming` (`false` on this path), `authenticated`, `encrypted`
- The same `write()` / `disconnect()` / `available()` / `read()` API as the server side

## Notes

- **`connect()` returning true only means the request was accepted.** The outcome arrives through `onConnected()` or `onConnectionFailed()`; a peer that is not listening ends in a timeout.
- **The first SPP service in SDP is used.** A device that publishes several serial ports cannot be steered to a specific one from this API yet.
- **Outgoing and incoming sessions share one API.** Session, write, data, and disconnect calls are identical; only `session.incoming` differs. That is also why one active session is the limit across both roles.
- **A reconnect gets a new session ID.** Do not cache the old ID across a disconnect.
- To get authentication and encryption on the client side, pass a security level to `connect()` and configure `EspBleConfig::classicSecurity` ([SppSecurity](../SppSecurity/)).

## Expected Serial output

```
Enter a Classic address such as 01:23:45:67:89:ab
connected: id=1 peer=d0:cf:13:58:fd:95
received 5 bytes on session 1
disconnected: id=1
```
