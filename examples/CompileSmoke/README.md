# CompileSmoke

> 日本語版: [README.ja.md](README.ja.md)

This minimal build check includes `EspBleBluedroid.h` and prints the library
version without initializing the Bluetooth stack.

```sh
arduino-cli compile --profile esp32 examples/CompileSmoke
```

It targets the original ESP32 configuration where Arduino-ESP32 provides the
Bluedroid backend.

