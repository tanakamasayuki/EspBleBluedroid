# Just Works Security Client

> 日本語版: [README.ja.md](README.ja.md)

Connects as a Central, starts LE Secure Connections Just Works pairing, and
stores the bond for encrypted reconnections. Security completion is delivered
from `update()` through `onSecurityChanged()`.

Call `deleteBond()` or `deleteAllBonds()` only after disconnecting. Bond removal
waits for Bluedroid's asynchronous persistent-store update before returning.
