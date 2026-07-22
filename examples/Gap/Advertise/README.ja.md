# Advertise

> English: [README.md](README.md)

EspBleBluedroidの公開APIでLocal Name、Battery Service UUID、Manufacturer Dataを
Legacy Advertisingへ掲載します。

```sh
arduino-cli compile --profile esp32 examples/Gap/Advertise
```

`bluetooth.update()`はduration管理と今後追加されるevent配送に必要です。
同じbit幅のService UUIDは単一のComplete Listへまとめられます。Legacy Advertising
またはScan Responseが31 bytesを超える構成では`start()`が`InvalidArgument`で失敗します。
