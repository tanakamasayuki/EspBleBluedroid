# Classic SPP Security

> 日本語版: [README.ja.md](README.ja.md)

This example starts an SPP Server that requires SSP authentication and link
encryption. Classic Security is configured once on `EspBleConfig`, while the
security requirement remains explicit on `EspBluedroidSppServerConfig`. This
separation lets future Classic profiles share pairing UI without inheriting
SPP policy.

When both devices use DisplayYesNo, the library delivers
`EspBluedroidClassicNumericComparison` from `bluetooth.update()`. Compare the
six-digit value on both devices and enter `y` or `n` in Serial Monitor.
Unanswered requests are rejected after `responseTimeoutMilliseconds`.

`EspBluedroidClassicSecurityChanged` reports the backend authentication result.
An established secure SPP session sets `authenticated` and `encrypted`.
Persisted Classic link keys are intentionally separate from BLE bonds and can
be managed with `classic().bondCount()`, `bond()`, `deleteBond()`, and
`deleteAllBonds()`.
The example targets the original ESP32 with Arduino-ESP32 3.3.11 and does not
require PSRAM.
