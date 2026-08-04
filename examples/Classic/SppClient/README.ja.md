# SppClient

> English: [README.md](README.md)
> 概念の説明: [Bluetooth Classic通信の入門ガイド](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) SPPの章
> EspBle: 対応exampleなし — Bluetooth Classic専用（[DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)）

SPPの接続する側です。Serial MonitorへClassic addressを入力すると、その機器のシリアルポートへ接続し、`hello`を送ります。

BLEと違い、`connect()`へ渡すscan resultはありません。**Classicはaddressで接続します。** addressは[Inquiry](../Inquiry/)、相手側のログ、スマートフォンのBluetooth設定などから入手してください。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（SPP client。接続する側）
- SPP server × 1 — 2台目のボードで[SppServer](../SppServer/)、またはClassicのシリアルポートを提供する任意の機器

## 動作

- stackを初期化し、入力を促すメッセージを表示します
- Serialからcanonical address（`01:23:45:67:89:ab`）を読み、`connect()`を呼びます
- `onConnected()`で`hello`を送信し、session IDとpeer addressを表示します
- 受信した各packetのbyte数を表示します
- 切断と、接続失敗（peer addressとerror詳細付き）を表示します

## 主なAPI

- `bluetooth.classic().spp().connect(address, timeoutMilliseconds, security)` — 要求を受理して即座に戻ります。SDP discoveryとRFCOMM接続は後で完了します
- `onConnected()` / `onDisconnected()` / `onData()` / `onConnectionFailed()` — `bluetooth.update()`から配送
- `EspBluedroidSppSession` — `id`、`peerAddress`、`incoming`（この経路では`false`）、`authenticated`、`encrypted`
- `write()` / `disconnect()` / `available()` / `read()`はServer側と同じAPI

## メモ

- **`connect()`のtrueは「要求を受理した」だけを意味します。** 結果は`onConnected()`または`onConnectionFailed()`で届きます。待受していない相手はtimeoutになります。
- **SDPが返す最初のSPP serviceを使います。** 複数のシリアルポートを公開する機器に対して、特定のポートを選ぶAPIはまだありません。
- **outgoingとincomingは同じAPIを共有します。** session、write、data、disconnectの呼び方は同じで、違うのは`session.incoming`だけです。同時1 sessionという上限が両roleを通じたものになっているのもこのためです。
- **再接続では新しいsession IDになります。** 切断をまたいで古いIDを保持しないでください。
- Client側で認証・暗号化を要求するには、`connect()`へsecurity levelを渡し、`EspBleConfig::classicSecurity`を設定します（[SppSecurity](../SppSecurity/)）。

## 期待されるSerial出力

```
Enter a Classic address such as 01:23:45:67:89:ab
connected: id=1 peer=d0:cf:13:58:fd:95
received 5 bytes on session 1
disconnected: id=1
```
