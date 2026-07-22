# GATT Read and Write

> 日本語版: [README.ja.md](README.ja.md)

Scans for the Battery Service, connects asynchronously, and requests a
Characteristic Read, then writes a binary value with a response. Request
acceptance is returned by `readCharacteristic()` / `writeCharacteristic()`;
copied, binary-safe results are delivered later from `update()` through their
respective callbacks.

The initial GATT Client supports one operation at a time. A request with an unknown
connection ID is rejected synchronously. Service/characteristic lookup and ATT
failures are reported asynchronously in `EspBleGattResult`.
