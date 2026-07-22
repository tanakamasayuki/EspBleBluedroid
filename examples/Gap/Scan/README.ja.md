# Scan

> English: [README.md](README.md)

EspBleBluedroidの公開APIでactive scanし、address、RSSI、Local Nameを表示します。
Scan Result callbackはBluedroid taskから直接呼ばれず、loopの`bluetooth.update()`から
配送されます。

```sh
arduino-cli compile --profile esp32 examples/Gap/Scan
```

