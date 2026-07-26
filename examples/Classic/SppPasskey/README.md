# Classic SPP Passkey

> 日本語版: [README.ja.md](README.ja.md)

This example starts an authenticated and encrypted SPP Server using Classic
SSP Passkey Entry. It defaults to `KeyboardOnly`: enter the six-digit value
shown by the peer into Serial Monitor.

Passkey display and request events are delivered from `bluetooth.update()` and
include the Classic peer address. `providePasskey(peerAddress, passkey)`
answers one pending KeyboardOnly request; its successful return means that the
reply was accepted, while `onSecurityChanged()` reports the pairing result.

Change the I/O capability to `DisplayOnly` when this ESP32 should display the
value for a peer with a keyboard. Persisted link keys can be removed through
the separate Classic bond API. The example targets original ESP32 with
Arduino-ESP32 3.3.11 and does not require PSRAM.
