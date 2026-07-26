# Classic SPP Stream

> 日本語版: [README.ja.md](README.ja.md)

This example wraps an established SPP session in
`EspBluedroidSppStream`, an Arduino `Stream` implementation. After
`attach()`, ordinary `available()`, `peek()`, `read()`, `write()`,
`print()`, and `println()` calls work without passing the session ID each
time. A session can also be supplied directly to the wrapper constructor.

The wrapper does not own the Bluetooth stack or session. `connected()` and
its boolean conversion become false when the bound session disconnects.
Calling `attach()` again replaces the binding, and `detach()` clears it.
`availableForWrite()` reports the remaining bounded queue capacity.
Large `write()` calls are split into the backend's 990-byte packets and return
the number of bytes accepted before the queue fills.
`flush()` waits until that queue has completed or the session disconnects.

This example starts an unauthenticated SPP Server and echoes received stream
bytes. It targets the original ESP32 and does not require PSRAM.
