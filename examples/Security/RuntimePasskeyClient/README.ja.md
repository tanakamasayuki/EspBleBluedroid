# Runtime Passkey Security Client

> English: [README.md](README.md)

passkeyをfirmwareへ保存せず、KeyboardOnlyのMITM pairingを行います。DisplayOnlyの
peerへ表示された6桁の値をSerial Monitorへ入力すると、`providePasskey()`で待機中の
Bluedroid pairingへ渡します。

passkey入力待ちは最大30秒です。`providePasskey()`はこの待機の前でも途中でも値を
受け付けるため、Serial、keypad、別のout-of-band interfaceから入力できます。
