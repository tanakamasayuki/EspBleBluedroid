# HfpHandsFree

> English: [README.md](README.md)
> 概念の説明: [Bluetooth Classic通信の入門ガイド](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) HFPの章
> EspBle: 対応exampleなし — Bluetooth Classic専用（[DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)）

**ヘッドセット側**として動作します。スマートフォン（Audio Gateway）へ接続し、音声linkを確立して、通話音声を双方向にやり取りします。

HFPには2つの層があり、ここではどちらも重要です。

| 層 | 何か | このsketchでは |
|---|---|---|
| **SLC**（Service Level Connection） | 制御チャネル。ATコマンドとindicator | `connect()`で確立し、`onConnected()`で通知 |
| **SCO** | 実際に音声を運ぶチャネル | `connectAudio()`で要求し、`onAudioChanged()`で通知 |

SLCが繋がっていることは、音声があることを**意味しません**。この分離があるため`connectAudio()`が独立した呼び出しになっています。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（HFP Hands-Free / ヘッドセットrole）
- Audio Gateway × 1 — スマートフォン、または2台目のボードで[HfpAudioGateway](../HfpAudioGateway/)
- 任意: 実際の音声にはI2Sマイクとスピーカー。exampleは無音を送り、受信した音声は破棄します

## 動作

- Hands-Free roleを開始します
- SerialからAudio Gatewayのaddressを読み、`connect()`を呼びます
- `onConnected()`で直ちに`connectAudio()`を呼び、SCOを確立します
- `onAudioChanged()`から音声の状態と交渉結果のsample rateを表示します
- 下り音声を`onPcmData()`で受け取り、上り（マイク）音声を`onPcmRequested()`で供給します

## 音声フォーマット

Core内蔵のcodecがCVSDまたはmSBCをそのままのPCMへ変換するため、sketchが扱うのは
**mono 16-bitサンプル**です。`event.format` / `request.format`に交渉結果の
`sampleRate`が入ります（CVSDは8 kHz、mSBCは16 kHz）。決め打ちせずこれを読んでください。

どちらのPCM callbackも**HFP stack taskで同期的に**走ります。bounded queue経由で
コピーし、ブロックせず、バッファをcallbackの外へ持ち出さないこと。
`onPcmRequested()`では`request.written`の設定が必須です。

## 主なAPI

- `bluetooth.classic().hfpHandsFree()` — `start()`、`stop()`、`connect(peerAddress)`、`disconnect(sessionId)`、`connectAudio(sessionId)`、`disconnectAudio(sessionId)`、`session(out)`
- `onConnected()` / `onDisconnected()` / `onStarted()` / `onConnectionFailed()` / `onAudioChanged()` — `update()`から配送
- `onPcmData()` / `onPcmRequested()` — stack taskの音声callback
- `EspBluedroidHfpSession` — `peerAddress`、`role`、`incoming`、`audioConnected`、`codec`、`format`
- `EspBluedroidHfpCodec` — `Cvsd`、`Msbc`、`Unknown`

## メモ

- **call controlは整備中です。** SLC、SCO、双方向PCMは動作し、peer testで確認しています。応答・拒否・発信はまだ公開していません。そのため`profileSupport()`はHFPを`LibraryNotImplemented`と報告します（[ProfileSupport](../ProfileSupport/)）。
- **Gateway側から自発的にSCOを確立することがあります**（着信時など）。どちらの経路でも、現在の状態を見る場所は`onAudioChanged()`の1か所です。
- **HFPとA2DPは別のprofileです。** 通話音声はHFP/SCO、音楽はA2DPで、スマートフォンは用途に応じて両方を使います。
- スマートフォン側では、Bluetooth設定でESP32をペアリングしておかないとHFP接続を受け付けないことが多いです。

## 期待されるSerial出力

```
audio=1 rate=8000
```
