# Scan

> 日本語版: [README.ja.md](README.ja.md)

Runs an active scan and prints each address, RSSI, and local name. Scan-result
callbacks are deferred from the Bluedroid task to `bluetooth.update()` in the
loop context.

