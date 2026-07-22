# Connect

> English: [README.md](README.md)

Battery Service (`180F`)をAdvertisingしているPeripheralをscanし、非同期に接続します。
`connect()`の戻り値は要求の受理だけを表し、完了・失敗・切断は`update()`からcallbackで
配送されます。接続後の操作にはbackend handleではなく`EspBleConnection::id`を使います。

初期実装はCentral 1接続です。GATT操作は今後追加します。
