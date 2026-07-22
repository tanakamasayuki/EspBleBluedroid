# GATT Read

> 日本語版: [README.ja.md](README.ja.md)

Scans for the Battery Service, connects asynchronously, and requests a
Characteristic Read. Request acceptance is returned by `readCharacteristic()`;
the copied, binary-safe value is delivered later from `update()` through
`onCharacteristicRead()`.

The initial GATT Client supports one operation at a time. A request with an unknown
connection ID is rejected synchronously. Service/characteristic lookup and ATT
failures are reported asynchronously in `EspBleGattResult`.
