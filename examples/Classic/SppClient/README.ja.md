# Classic SPP Client

> English: [README.md](README.md)

Serial MonitorへBluetooth Classic SPP Serverのcanonical addressを入力します。
`connect()`は非同期要求を受理するだけで、SDP discoveryとRFCOMM接続の完了は
`onConnected()`または`onConnectionFailed()`から後で配送されます。

outgoingとincoming接続は同じ`EspBluedroidSppSession`、`write()`、`onData()`、
`disconnect()` APIを使います。Client経路では`incoming`が`false`になります。

現在のClientはSDPが返す最初のSPP serviceを利用し、pendingまたはactive session
1つに対応します。SPP認証は未実装です。
