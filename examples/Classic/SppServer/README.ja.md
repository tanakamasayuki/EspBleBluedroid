# SppServer

> English: [README.md](README.md)
> 概念の説明: [Bluetooth Classic通信の入門ガイド](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) SPPの章
> EspBle: 対応exampleなし — Bluetooth Classic専用（[DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)）

**認証なしのSerial Port Profile server**を開始し、受信したpacketをそのままechoします。

SPPは「とにかくバイト列を送りたい」に対するClassicの答えです。RFCOMMが双方向のbyte streamを提供し、スマートフォンやPCからはシリアルポートとして見えます。GATT databaseもattributeのpermissionもありません。だからこそsecurityは別ステップのexample（[SppSecurity](../SppSecurity/)、[SppPasskey](../SppPasskey/)）になっています。

同じServerを`Stream`形式で扱う版は[SppSerialServer](../SppSerialServer/)です。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（SPP server。待受側）
- SPP client × 1 — 2台目のボードで[SppClient](../SppClient/)、Classicのシリアルポートへ接続できるスマートフォンのターミナルアプリ、またはPCのBluetoothシリアルポート

## 動作

- `begin()`の前に`capabilities().classicSpp`を確認します
- service name `EspBleBluedroid SPP`でSPP serverを開始します。これはSDPを通じて相手に見える名前です
- 接続・切断eventをsession IDとpeer address付きで表示します
- 受信した各packetを`write(sessionId, value)`でechoします
- writeが拒否された場合は`lastErrorName()`を表示します

## 主なAPI

- `bluetooth.classic().spp().startServer(config)` — `EspBluedroidSppServerConfig::serviceName`、`channel`（0はstackにRFCOMM channelを選ばせる）、`security`
- `onServerStarted()` / `onConnected()` / `onDisconnected()` / `onData()` — すべて`bluetooth.update()`から配送
- `write(sessionId, value)` / `write(sessionId, data, length)` — 1回あたり1〜990 byte
- `disconnect(sessionId)` / `session(sessionId, out)` / `sessionCount()`
- `pendingWriteCount()` / `droppedWriteCount()` / `droppedReceiveByteCount()` / `droppedEventCount()` — 固定長リソースの診断
- `available(sessionId)` / `peek(sessionId)` / `read(sessionId)` — `onData()`を待たずに読める受信ring

## 固定長リソース

| リソース | 上限 | 溢れたときの見え方 |
|---|---|---|
| session | pendingまたはactiveで1つ | `startServer()`は同時に1台のみ受け付ける |
| 送信queue | 8件 | `write()`がfalseを返し、`droppedWriteCount()`が増える |
| 1 writeのサイズ | 1〜990 byte | `write()`が`InvalidArgument`でfalseを返す |
| 受信ring | 2048 byte | 既存byteを保持し、超過分は`droppedReceiveByteCount()`で数える |

## メモ

- **SPPのdataはbinary-safeです。** eventはコピーされた`String`を所有するため、途中のNULも保持されます。C文字列関数ではなく`value.length()`とindex accessを使ってください。
- **callbackはBluedroid callbackではなく`update()`から届きます。** そのため`onData()`の中でechoしても安全です。writeはqueueへ入るだけで、stack contextから送信するわけではありません。
- **`onWriteCompleted()`はbackendがwriteを完了した時点を知らせます。** session ID、byte数、error詳細付きです。切断時にまだqueueに残っていたwriteは完了eventの対象になりません。
- **同じバイト列を読む方法が2つあります。** `onData()`はpacket event、`available()` / `read()`はringからのbyte streamです。二重に消費しないよう、sketchごとにどちらかへ寄せてください。
- 相手には設定した`serviceName`が見えます。複数のボードが同じ部屋にあるときは分かる名前にしてください。

## 期待されるSerial出力

```
SPP server started
connected: id=1 peer=20:32:c6:1e:9d:4a
received 5 bytes
received 12 bytes
disconnected: id=1
```
