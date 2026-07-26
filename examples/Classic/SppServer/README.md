# Classic SPP Server

> 日本語版: [README.ja.md](README.ja.md)

This example starts an unauthenticated Bluetooth Classic Serial Port Profile
server and echoes each received packet. Client and server connections use the
same `EspBluedroidSppSession` model; this first slice implements the server
side.

SPP data is binary-safe. Events own a copied `String`, so embedded NUL bytes
remain available through `value.length()` and indexing. Callbacks run from
`bluetooth.update()`, not from the Bluedroid callback.

The current implementation supports one active SPP session and one pending
write of 1–990 bytes. A later write can be submitted after the peer receives
the previous one. Client connection and SPP security will be added separately.
