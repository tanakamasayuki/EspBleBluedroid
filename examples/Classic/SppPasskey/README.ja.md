# Classic SPP Passkey

> English: [README.md](README.md)

Classic SSP Passkey Entryを使う、認証・暗号化必須のSPP Server exampleです。既定は
`KeyboardOnly`で、peerに表示された6桁値をSerial Monitorへ入力します。

Passkey表示・入力要求はClassic peer address付きで`bluetooth.update()`から配送されます。
`providePasskey(peerAddress, passkey)`は待機中のKeyboardOnly要求1件へ回答します。
戻り値の成功は回答が受理されたことを意味し、pairing結果は
`onSecurityChanged()`で確定します。

ESP32側に値を表示し、keyboardを持つpeerへ入力する場合はI/O capabilityを
`DisplayOnly`へ変更します。保存link keyはBLEとは別のClassic bond APIで削除できます。
対象はArduino-ESP32 3.3.11の無印ESP32で、PSRAMは不要です。
