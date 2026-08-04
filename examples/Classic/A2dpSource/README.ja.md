# A2dpSource

> English: [README.md](README.md)
> 概念の説明: [Bluetooth Classic通信の入門ガイド](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) A2DPの章
> EspBle: 対応exampleなし — Bluetooth Classic専用（[DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)）

Bluetoothスピーカーやヘッドセット**へ**音声を送ります。Sink側のClassic addressを入力すると接続してstreamを開始し、要求に応じてPCMを供給します。ここでは無音を返すので、自分の音源を差し込めます。あわせて**AVRCP Target**も動かすため、スピーカー側の再生・一時停止・音量操作がこのsketchへ届きます。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（A2DP Source）
- A2DP Sink × 1 — Bluetoothスピーカーやヘッドセット、または2台目のボードで[A2dpSink](../A2dpSink/)

Sinkをペアリングモードにして、addressは[Inquiry](../Inquiry/)で調べてください。

## 動作

- AVRCP Targetを開始し、届いた再生操作とabsolute volume要求を表示します
- A2DP Source roleを開始します
- SerialからClassic addressを読み、`connect()`を呼びます
- `onConnected()`で`startStream()`を呼びます
- PCM要求のたびに無音で埋め、`request.written`を設定します

## PCM要求callback

```cpp
source.onPcmRequested([](EspBluedroidA2dpPcmRequest &request) {
  if (request.flush) return;                       // queue済み音声を破棄する
  memset(request.data, 0, request.capacity);       // ここへ自分のサンプルを入れる
  request.written = request.capacity;              // capacity以下にする
});
```

- **A2DP stack taskで同期的に走ります。** 速やかに戻り、ブロックしないこと
- **`request.written`の設定が必須です。** 0のままだと何も送られません
- **`request.flush == true`はバッファ済み音声の破棄を意味します**（シーク後など）。queueとリサンプラの状態をクリアし、書き込まずに戻ってください
- **`request.format`を読んでください。** 決め打ちせず、交渉結果のsample rateとchannel数に従います。encoderはそのformatの16-bit interleavedサンプルを期待します

## 主なAPI

- `bluetooth.classic().a2dpSource()` — `start()`、`stop()`、`connect(peerAddress)`、`disconnect(sessionId)`、`startStream()`、`suspendStream()`、`session(out)`
- `onPcmRequested()` — 上記のstack task callback
- `onConnected()` / `onDisconnected()` / `onStarted()` / `onConnectionFailed()` / `onStreamChanged()` — `update()`から配送
- `bluetooth.classic().avrcpTarget()` — `onCommand()`、`onAbsoluteVolumeRequested()`、`setAbsoluteVolume(volume)`、`absoluteVolume()`

## メモ

- **A2DP roleは同時に1つだけ**なので、1つのsketchで[A2dpSink](../A2dpSink/)と併用することはできません。
- **`startStream()`は接続とは別の操作です。** 接続済みsessionはまだ再生中ではありません。遷移は`onStreamChanged()`で報告されます。
- **スピーカーのボタンはAVRCP Target経由で届きます。** 音量要求は`onAbsoluteVolumeRequested()`として届き、アプリ側から音量を変える場合は`setAbsoluteVolume()`で通知します。
- 無音を流すだけでも経路全体は動きます。Sink側にはstream接続中として見えるので、実際の音源を足す前の立ち上げ確認に有用です。
- Arduino-ESP32 3.3.11の制約は[docs/CLASSIC_PROFILE_SUPPORT.ja.md](../../../docs/CLASSIC_PROFILE_SUPPORT.ja.md)を参照してください。

## 期待されるSerial出力

```
Connected: 41:42:d8:70:1c:33, session=1
AVRCP command: command=70 state=0
AVRCP volume: 96
Disconnected: session=1
```
