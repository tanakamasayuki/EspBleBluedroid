# Connect

> 日本語版: [README.ja.md](README.ja.md)

Scans for a peripheral advertising the Battery Service (`180F`) and connects
asynchronously. `connect()` only reports request acceptance; completion,
failure, and disconnection callbacks are dispatched from `update()`. Use the
stable `EspBleConnection::id`, not a backend handle, for subsequent operations.

The initial implementation supports one Central connection. GATT operations
will be added later.
Reconnecting after a disconnection issues a new connection ID. Unreachable-peer
and backend failures after request acceptance arrive asynchronously through
`onConnectionFailed()`.
Calling `end()` during an in-flight request suppresses that request's completion
callback and may wait for up to approximately one second while the current
Bluedroid connection-wait slice is released.
