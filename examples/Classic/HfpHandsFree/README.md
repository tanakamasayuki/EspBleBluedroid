# HfpHandsFree

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [Bluetooth Classic beginner guide (Japanese)](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) — HFP
> EspBle: no counterpart — Bluetooth Classic only ([DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md))

Acts as a **headset**: connects to a phone (the Audio Gateway), brings up the voice link, and moves mono call audio in both directions.

HFP has two layers, and both matter here:

| Layer | What it is | In this sketch |
|---|---|---|
| **SLC** (Service Level Connection) | The control channel — AT commands, indicators | Established by `connect()`; reported by `onConnected()` |
| **SCO** | The voice channel that actually carries audio | Requested with `connectAudio()`; reported by `onAudioChanged()` |

A connected SLC does **not** mean there is audio. That separation is why `connectAudio()` exists as its own call.

## Hardware

- 1 × original ESP32 running this sketch (HFP Hands-Free / headset role)
- 1 × Audio Gateway — a phone, or a second board running [HfpAudioGateway](../HfpAudioGateway/)
- Optional: I2S microphone and speaker for real audio; the example sends silence and discards what it receives

## What it does

- Starts the Hands-Free role
- Reads an Audio Gateway address from Serial and calls `connect()`
- On `onConnected()`, immediately calls `connectAudio()` to bring up SCO
- Prints the audio state and the negotiated sample rate from `onAudioChanged()`
- Receives downlink audio in `onPcmData()` and supplies uplink (microphone) audio in `onPcmRequested()`

## Audio format

The Core's built-in codec converts CVSD or mSBC to plain PCM, so the sketch sees
**mono 16-bit samples**. `event.format` / `request.format` carry the negotiated
`sampleRate` — 8 kHz for CVSD, 16 kHz for mSBC — so read it rather than assuming.

Both PCM callbacks run **synchronously on the HFP stack task**: copy through a
bounded queue, never block, and never retain the buffer past the callback.
`onPcmRequested()` must set `request.written`.

## Key APIs

- `bluetooth.classic().hfpHandsFree()` — `start()`, `stop()`, `connect(peerAddress)`, `disconnect(sessionId)`, `connectAudio(sessionId)`, `disconnectAudio(sessionId)`, `session(out)`
- `onConnected()` / `onDisconnected()` / `onStarted()` / `onConnectionFailed()` / `onAudioChanged()` — delivered from `update()`
- `onPcmData()` / `onPcmRequested()` — the stack-task audio callbacks
- `EspBluedroidHfpSession` — `peerAddress`, `role`, `incoming`, `audioConnected`, `codec`, `format`
- `EspBluedroidHfpCodec` — `Cvsd`, `Msbc`, `Unknown`

## Notes

- **Call control is still being implemented.** SLC, SCO, and bidirectional PCM work and are covered by peer tests; answering, rejecting, and dialling are not exposed yet, which is why `profileSupport()` reports `LibraryNotImplemented` for HFP ([ProfileSupport](../ProfileSupport/)).
- **The gateway may bring SCO up on its own** when a call starts; `onAudioChanged()` is the single place to observe the current state either way.
- **HFP and A2DP are different profiles.** Call audio is HFP/SCO, music is A2DP — a phone uses both, for different things.
- A phone usually needs the ESP32 paired in its Bluetooth settings before it will accept the HFP connection.

## Expected Serial output

```
audio=1 rate=8000
```
