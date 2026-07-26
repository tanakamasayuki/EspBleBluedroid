# Classic SPP Server

> English: [README.md](README.md)

認証なしのBluetooth Classic Serial Port Profile serverを開始し、受信packetを
echoするexampleです。Client接続とServer接続は同じ
`EspBluedroidSppSession` modelを使い、最初のsliceではServer側を実装しています。

SPP dataはbinary-safeです。eventはcopyされた`String`を所有するため、embedded NULも
`value.length()`とindex accessで保持されます。callbackはBluedroid callbackではなく
`bluetooth.update()`から配送されます。

現在はactive SPP session 1つ、8件の送信queue、1 writeあたり1〜990 byteに対応します。
`pendingWriteCount()`と`droppedWriteCount()`で固定長queueの状態を確認できます。
SPP Securityは別のテストsliceで追加します。
