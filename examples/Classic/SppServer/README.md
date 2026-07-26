# Classic SPP Server

> 日本語版: [README.ja.md](README.ja.md)

This example starts an unauthenticated Bluetooth Classic Serial Port Profile
server and echoes each received packet. Client and server connections use the
same `EspBluedroidSppSession` model; this first slice implements the server
side.

SPP data is binary-safe. Events own a copied `String`, so embedded NUL bytes
remain available through `value.length()` and indexing. Callbacks run from
`bluetooth.update()`, not from the Bluedroid callback.

The current implementation supports one active SPP session and an eight-entry
write queue. Each write contains 1–990 bytes. Use `pendingWriteCount()` and
`droppedWriteCount()` for bounded-queue diagnostics. A separate 2048-byte
receive ring provides session-scoped `available()`, `peek()`, and `read()`
without waiting for `update()`; `droppedReceiveByteCount()` reports bytes
rejected while that ring was full. SPP security will be added separately.
