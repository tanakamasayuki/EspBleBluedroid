# Classic SPP Serial Server

> 日本語: [README.ja.md](README.ja.md)

This example bridges an SPP Server and the board `Serial` port through
`EspBluedroidSppSerial`.

```cpp
EspBleBluedroid bluetooth;
EspBluedroidSppSerial sppSerial(bluetooth);
```

After an incoming session connects, the usual Arduino `Stream`/`Print`
operations are available. The wrapper follows the active session, so no
session-ID storage or application-side binding is needed. `connected()` becomes false
on disconnect and true when the next incoming session is established.

It targets original ESP32 boards and Arduino-ESP32 3.3.11 and needs no PSRAM.
