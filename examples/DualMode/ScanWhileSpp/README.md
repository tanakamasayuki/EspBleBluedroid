# BLE Scan While SPP Is Connected

> 日本語版: [README.ja.md](README.ja.md)

Enter a Classic SPP Server address. After the SPP session connects, this
example performs a ten-second active BLE Scan without dropping the Classic
session.

BLE Scan and Classic SPP remain separate APIs and result types even though the
root object owns one dual-mode Bluedroid stack. Keep calling `update()` for
both event paths.

Two-board automation additionally verifies BLE GATT notifications and binary
SPP traffic over consecutive rounds without reconnecting or resubscribing.
