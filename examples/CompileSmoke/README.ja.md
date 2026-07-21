# CompileSmoke

> English: [README.md](README.md)

最小のビルド確認用sketchです。`EspBleBluedroid.h`をincludeしてライブラリの
versionを表示します。Bluetooth stackは初期化しません。

```sh
arduino-cli compile --profile esp32 examples/CompileSmoke
```

対象はArduino-ESP32がBluedroidを提供する無印ESP32です。

期待されるSerial出力:

```text
EspBleBluedroid 0.1.0
```

