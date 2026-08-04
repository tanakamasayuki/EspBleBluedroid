# A2dpSource

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [Bluetooth Classic beginner guide (Japanese)](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) — A2DP
> EspBle: no counterpart — Bluetooth Classic only ([DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md))

Sends audio **to** a Bluetooth speaker or headset. Type the sink's Classic address, and the sketch connects, starts the stream, and feeds PCM on demand — silence here, so you can drop in your own source. It also runs an **AVRCP Target**, so the speaker's play / pause / volume buttons reach this sketch.

## Hardware

- 1 × original ESP32 running this sketch (A2DP Source)
- 1 × A2DP Sink — a Bluetooth speaker or headset, or a second board running [A2dpSink](../A2dpSink/)

Put the sink in pairing mode and get its address from [Inquiry](../Inquiry/).

## What it does

- Starts the AVRCP Target and prints incoming transport commands and absolute-volume requests
- Starts the A2DP Source role
- Reads a Classic address from Serial and calls `connect()`
- On `onConnected()`, calls `startStream()`
- Fills every PCM request with silence and sets `request.written`

## The PCM request callback

```cpp
source.onPcmRequested([](EspBluedroidA2dpPcmRequest &request) {
  if (request.flush) return;                       // drop queued audio
  memset(request.data, 0, request.capacity);       // your samples go here
  request.written = request.capacity;              // must be <= capacity
});
```

- **Runs synchronously on the A2DP stack task.** Return quickly; do not block
- **`request.written` must be set** — leave it 0 and nothing is sent
- **`request.flush == true` means discard buffered audio**, e.g. after a seek. Clear your queue and any resampler state, and return without writing
- **Read `request.format`** for the negotiated sample rate and channel count instead of assuming; the encoder expects 16-bit interleaved samples in that format

## Key APIs

- `bluetooth.classic().a2dpSource()` — `start()`, `stop()`, `connect(peerAddress)`, `disconnect(sessionId)`, `startStream()`, `suspendStream()`, `session(out)`
- `onPcmRequested()` — the stack-task callback above
- `onConnected()` / `onDisconnected()` / `onStarted()` / `onConnectionFailed()` / `onStreamChanged()` — from `update()`
- `bluetooth.classic().avrcpTarget()` — `onCommand()`, `onAbsoluteVolumeRequested()`, `setAbsoluteVolume(volume)`, `absoluteVolume()`

## Notes

- **Only one A2DP role may be active**, so this cannot run alongside [A2dpSink](../A2dpSink/) in one sketch.
- **`startStream()` is separate from connecting.** A connected session is not yet a playing one; `onStreamChanged()` reports the transition.
- **AVRCP Target is how the speaker's buttons reach you.** A volume request arrives as `onAbsoluteVolumeRequested()`; answer with `setAbsoluteVolume()` when the application changes volume itself.
- Feeding silence still exercises the whole path — the sink shows a connected stream, which makes this a useful first bring-up step before adding a real audio source.
- See [docs/CLASSIC_PROFILE_SUPPORT.ja.md](../../../docs/CLASSIC_PROFILE_SUPPORT.ja.md) for Arduino-ESP32 3.3.11 constraints.

## Expected Serial output

```
Connected: 41:42:d8:70:1c:33, session=1
AVRCP command: command=70 state=0
AVRCP volume: 96
Disconnected: session=1
```
