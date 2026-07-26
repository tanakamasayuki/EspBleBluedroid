# Classic Inquiry

> 日本語版: [README.ja.md](README.ja.md)

This example discovers nearby discoverable Bluetooth Classic devices. It
checks the compile-time capability snapshot before initializing the shared
Bluedroid stack, then prints each copied Inquiry Result from `update()`.

Classic Inquiry is intentionally separate from BLE Scan:
`bluetooth.classic().inquiry()` returns Classic address, name, Class of Device,
and RSSI fields, while `bluetooth.scanner()` returns BLE advertising data.

Keep calling `bluetooth.update()` to receive result and completion callbacks.
`stop()` requests cancellation; the later completion event reports
`cancelled=true`.
