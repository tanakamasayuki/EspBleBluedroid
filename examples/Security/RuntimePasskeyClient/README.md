# Runtime Passkey Security Client

> 日本語版: [README.ja.md](README.ja.md)

Uses KeyboardOnly MITM pairing without storing a passkey in the firmware.
Connect a DisplayOnly peer, read its six-digit value, and enter that value in
the Serial Monitor. The sketch passes it to the pending Bluedroid pairing with
`providePasskey()`.

The passkey request remains pending for up to 30 seconds. `providePasskey()`
accepts an input before or during that interval, so an application may obtain
the value from Serial, a keypad, or another out-of-band interface.
