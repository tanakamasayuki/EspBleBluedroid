# Classic SPP Client

> 日本語版: [README.ja.md](README.ja.md)

Enter the canonical address of a Bluetooth Classic SPP Server in Serial Monitor.
`connect()` only accepts the asynchronous request; SDP discovery and RFCOMM
connection complete later through `onConnected()` or `onConnectionFailed()`.

Outgoing and incoming connections use the same `EspBluedroidSppSession`,
`write()`, `onData()`, and `disconnect()` API. The `incoming` field is `false`
for this Client path.

The current Client uses the first SPP service returned by SDP and supports one
pending or active session. SPP authentication is not implemented yet.
