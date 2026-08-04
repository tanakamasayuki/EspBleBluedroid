# HfpAudioGateway

> English: [README.md](README.md)
> 概念の説明: [Bluetooth Classic通信の入門ガイド](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) HFPの章
> EspBle: 対応exampleなし — Bluetooth Classic専用（[DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)）

HFPのもう半分です。このボードが**スマートフォン側**のroleを演じ、ヘッドセットからの接続を待って通話音声を双方向に運びます。

スマートフォンを介さずヘッドセットを立ち上げたいときに使えます。2台目のボードで[HfpHandsFree](../HfpHandsFree/)を動かせば、両側とも自分で制御できるコードになります。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（HFP Audio Gateway / スマートフォンrole）
- Hands-Free機器 × 1 — Bluetoothヘッドセット、または2台目のボードで[HfpHandsFree](../HfpHandsFree/)

## 動作

- Audio Gateway roleを開始します。これでClassicが**connectableかつdiscoverable**になります。ここでは`connect()`を呼ぶ必要がありません
- ヘッドセットがSLCを確立したらpeer addressを表示します
- ヘッドセットのマイク音声を`onPcmData()`で受け取ります
- 下り通話音声を`onPcmRequested()`で供給します（このexampleでは無音）

## こちら側から見た音声の向き

| callback | 向き | 現実世界での対応 |
|---|---|---|
| `onPcmData()` | ヘッドセット → このボード | 利用者がマイクへ話した音声 |
| `onPcmRequested()` | このボード → ヘッドセット | 通話相手の音声 |

どちらも**HFP stack taskで同期的に**走ります。bounded queue経由でコピーし、
ブロックせず、要求callbackでは`request.written`を設定してください。mono 16-bit
サンプルで、交渉結果のrateは`request.format`から読みます（CVSD 8 kHz / mSBC 16 kHz）。

## 主なAPI

- `bluetooth.classic().hfpAudioGateway()` — `start()`、`stop()`、`connect(peerAddress)`、`disconnect(sessionId)`、`connectAudio(sessionId)`、`disconnectAudio(sessionId)`、`session(out)`
- `onConnected()` / `onDisconnected()` / `onStarted()` / `onConnectionFailed()` / `onAudioChanged()` — `update()`から配送
- `onPcmData()` / `onPcmRequested()` — stack taskの音声callback
- `EspBluedroidHfpSession` — この経路では`incoming`が`true`

## メモ

- **roleを開始すると機器がdiscoverableになります。** これによりヘッドセットから見つけて接続できるようになり、Hands-Free側との一番の挙動差になります。
- **SCOはどちらの側からでも確立できます。** ここにも`connectAudio()`があり、どちらが起動しても状態は`onAudioChanged()`で報告されます。
- **call indicatorとcall controlは整備中です。** そのためこのexampleは、着信や通話中を通知せずに音声だけを運びます。
- HFP sessionは同時に1つだけです。

## 期待されるSerial出力

```
HF connected: 41:42:d8:70:1c:33
```
