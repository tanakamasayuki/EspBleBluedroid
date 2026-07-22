# GATT Read / Write

> English: [README.md](README.md)

Battery Serviceをscanして非同期接続し、Characteristic Readの後にbinary値を
Write With Responseで書き込みます。`readCharacteristic()`と
`writeCharacteristic()`の戻り値は要求の受理を表し、binary-safeにcopyされた結果は
`update()`からそれぞれのcallbackへ配送されます。

初期GATT Clientは同時1操作です。存在しないconnection IDは同期的に拒否し、
Service/Characteristic検索やATTの失敗は`EspBleGattResult`で非同期に通知します。
