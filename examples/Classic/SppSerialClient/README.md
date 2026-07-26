# Classic SPP Serial Client

> 日本語: [README.ja.md](README.ja.md)

This example bridges an outgoing SPP Client session and the board `Serial` port
through `EspBluedroidSppSerial`.

```cpp
EspBleBluedroid bluetooth;
EspBluedroidSppSerial sppSerial(bluetooth);
```

While disconnected, enter the peer Classic address in the Serial Monitor to
start the existing asynchronous `classic().spp().connect()` operation. The
wrapper becomes usable after connection, unavailable on disconnect, and
follows the new session ID after reconnection.

It targets original ESP32 boards and Arduino-ESP32 3.3.11 and needs no PSRAM.
