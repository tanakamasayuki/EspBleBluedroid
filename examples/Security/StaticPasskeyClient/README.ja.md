# Static Passkey Security Client

> English: [README.md](README.md)

固定6桁passkeyでMITM保護します。この例はDisplayOnly側で、表示された値を
KeyboardOnlyのpeerへ入力します。

`onPasskeyDisplayed()`と`onSecurityChanged()`は`update()`から呼ばれます。静的
passkeyは組込みpeerには便利ですが共有secretとして扱い、実製品ではexample値から
必ず変更してください。
