# Just Works Security Client

> English: [README.md](README.md)

Centralとして接続し、LE Secure ConnectionsのJust Works pairingを開始して、次回の
暗号化再接続に使うbondを保存します。Security完了は`onSecurityChanged()`へ
`update()`から配送されます。

`deleteBond()` / `deleteAllBonds()`は切断後に呼びます。bond削除はBluedroidの
永続store更新完了を待ってから返ります。
