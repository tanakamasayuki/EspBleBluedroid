# Classic SPP Client

> English: [README.md](README.md)

Serial MonitorへBluetooth Classic SPP Serverのcanonical addressを入力します。
`connect()`は非同期要求を受理するだけで、SDP discoveryとRFCOMM接続の完了は
`onConnected()`または`onConnectionFailed()`から後で配送されます。

outgoingとincoming接続は同じ`EspBluedroidSppSession`、`write()`、`onData()`、
`disconnect()` APIを使います。Client経路では`incoming`が`false`になります。

現在のClientはSDPが返す最初のSPP serviceを利用し、pendingまたはactive session
1つに対応します。受信byteは遅延配送される`onData()` packet eventのほか、共通の
session別2048 byte受信ringから`available()`、`peek()`、`read()`で読み出せます。
SPP認証は未実装です。
