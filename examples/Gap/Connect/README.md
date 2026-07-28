# Connect

> 日本語版: [README.ja.md](README.ja.md)

Scans for a Peripheral advertising Battery Service (`0x180F`) and connects
asynchronously.

## Requirements

- One original ESP32 running this Central sketch
- A Peripheral advertising Battery Service

## Behavior

- Selects the peer by advertised Service UUID
- Stops scanning and submits a connection request
- Handles connection, asynchronous failure, and disconnection separately

## Main APIs

- `scanResult.advertisesService()` / `connect(scanResult)`
- `onConnected()` / `onConnectionFailed()` / `onDisconnected()`
- `EspBleConnection::id` / `disconnectReason`

The current public API supports one Central connection. A reconnect receives a
new connection ID.

## Expected Serial output

```text
Connected: id=1 peer=00:11:22:33:44:55 mtu=23
Disconnected: id=1
```
