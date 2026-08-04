# A2dpSink

> English: [README.md](README.md)
> 概念の説明: [Bluetooth Classic通信の入門ガイド](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) A2DPの章
> EspBle: 対応exampleなし — Bluetooth Classic専用（[DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)）

スマートフォンやPC（A2DP **Source**）から音楽を受け取り、デコード済みの音声を16-bit interleaved PCMとしてsketchへ渡します。あわせて**AVRCP Controller**も開始するので、このボードから再生操作を送り、その応答を確認できます。

SBCのデコードはCoreが行い、`onPcmData()`へ届く時点で既にPCMです。それを音にするのはアプリの仕事です（I2SでDACへ、USB Audio、VUメーター、あるいは何もしない）。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（A2DP Sink。オーディオ機器としてdiscoverableになります）
- A2DP Source × 1 — スマートフォン、タブレット、PC。Bluetooth設定からペアリングして再生してください
- 任意: 音を出したい場合はI2S DAC。example自体は受信のみです

## 動作

- `EspBleBluedroid Audio Sink`としてstackを初期化します
- AVRCP Controllerを開始し、接続とcommand応答のeventを表示します
- A2DP Sink roleを開始します。これでオーディオ接続を受け付ける状態になります
- 接続・切断をpeer addressとsession ID付きで表示します
- `onPcmData()`でPCMを受け取ります。実際のルールどおりcallbackを短く保つため、あえて何もしていません

## PCM callbackのルール

```cpp
sink.onPcmData([](const EspBluedroidA2dpPcmData &pcm) {
  // A2DP stack taskで走る。pcm.dataはこのcallbackを抜けるまでのみ有効。
});
```

- **`update()`ではなくA2DP stack taskで走ります**
- **`pcm.data`はcallbackから戻ると無効になります。** コピーしてください。ポインタを保持してはいけません
- **ブロックしないこと。** ここで待つとオーディオのパイプラインが止まり、音切れになります
- 正しい形は、ここでbounded queueへコピーし、I2S / USB / DSPの処理は別taskで行うことです

`pcm.format`には交渉結果の`sampleRate`、`channels`、`bitsPerSample`、`interleaved`が入ります。44.1 kHzステレオと決め打ちせず、これを読んでください。

## 主なAPI

- `bluetooth.classic().a2dpSink()` — `start()`、`stop()`、`started()`、`connect(peerAddress)`、`disconnect(sessionId)`、`session(out)`
- `onConnected()` / `onDisconnected()` / `onStarted()` / `onConnectionFailed()` / `onStreamChanged()` — `update()`から配送
- `onPcmData()` — 上記のstack task callback
- `EspBluedroidA2dpSession` — `peerAddress`、`role`、`incoming`、`streaming`、`audioMtu`、`codec`
- `bluetooth.classic().avrcpController()` — `click(command)`、`sendCommand(command, state)`、`setAbsoluteVolume(volume)`、`onCommandResponse()`、`onAbsoluteVolumeChanged()`

## メモ

- **A2DP roleは同時に1つだけです。** SinkとSourceを同時に動かすことはできず、後から`start()`した側が失敗します。
- **AVRCPはA2DPとは別のprofileです。** 音声はA2DP、再生操作はAVRCPを通ります。相手が片方だけ対応していることもあります。
- **streamの開始・停止は`onStreamChanged()`で届きます。** Source側の再生・一時停止がこちらからはこう見えます。
- Target側のmetadataやplay status応答は未実装です。ここのControllerはcommandを送り応答を観測します。
- Arduino-ESP32 3.3.11の制約はprofile対応表を参照してください: [docs/CLASSIC_PROFILE_SUPPORT.ja.md](../../../docs/CLASSIC_PROFILE_SUPPORT.ja.md)。

## 期待されるSerial出力

```
AVRCP Controller connected
Connected: 20:32:c6:1e:9d:4a, session=1
AVRCP response: command=68 state=1 accepted=1
Disconnected: session=1
```
