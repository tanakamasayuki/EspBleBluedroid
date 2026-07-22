# GATT Read

> English: [README.md](README.md)

Battery Serviceをscanして非同期接続し、Characteristic Readを要求します。
`readCharacteristic()`の戻り値は要求の受理を表し、binary-safeにcopyされた値は
`update()`から`onCharacteristicRead()`へ配送されます。

初期GATT Clientは同時1操作です。存在しないconnection IDは同期的に拒否し、
Service/Characteristic検索やATTの失敗は`EspBleGattResult`で非同期に通知します。
