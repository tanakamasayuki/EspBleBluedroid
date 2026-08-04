# A2dpSink

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [Bluetooth Classic beginner guide (Japanese)](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) — A2DP
> EspBle: no counterpart — Bluetooth Classic only ([DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md))

Receives music from a phone or PC (the A2DP **Source**) and hands the decoded audio to the sketch as 16-bit interleaved PCM. Also starts an **AVRCP Controller**, so this board can send transport commands and see their responses.

The Core decodes SBC; what reaches `onPcmData()` is already PCM. Turning that into sound is the application's job — I2S to a DAC, USB Audio, a VU meter, or nothing at all.

## Hardware

- 1 × original ESP32 running this sketch (A2DP Sink, discoverable as an audio device)
- 1 × A2DP Source — a phone, tablet, or PC. Pair from its Bluetooth settings and play something
- Optional: an I2S DAC if you want audible output; the example itself only receives

## What it does

- Initialises the stack as `EspBleBluedroid Audio Sink`
- Starts the AVRCP Controller and prints connection and command-response events
- Starts the A2DP Sink role, which makes the device connectable for audio
- Prints connect and disconnect with the peer address and session ID
- Receives PCM in `onPcmData()` — deliberately doing nothing with it, so the callback stays as short as the real rule requires

## The PCM callback rule

```cpp
sink.onPcmData([](const EspBluedroidA2dpPcmData &pcm) {
  // Runs on the A2DP stack task. pcm.data is valid only until this returns.
});
```

- **It runs on the A2DP stack task**, not from `update()`
- **`pcm.data` becomes invalid when the callback returns.** Copy it, do not store the pointer
- **Do not block.** Blocking here stalls the audio pipeline and produces dropouts
- The correct shape is: copy into a bounded queue here, and do I2S / USB / DSP work on another task

`pcm.format` carries the negotiated `sampleRate`, `channels`, `bitsPerSample`, and `interleaved` — read it rather than assuming 44.1 kHz stereo.

## Key APIs

- `bluetooth.classic().a2dpSink()` — `start()`, `stop()`, `started()`, `connect(peerAddress)`, `disconnect(sessionId)`, `session(out)`
- `onConnected()` / `onDisconnected()` / `onStarted()` / `onConnectionFailed()` / `onStreamChanged()` — delivered from `update()`
- `onPcmData()` — the stack-task audio callback described above
- `EspBluedroidA2dpSession` — `peerAddress`, `role`, `incoming`, `streaming`, `audioMtu`, `codec`
- `bluetooth.classic().avrcpController()` — `click(command)`, `sendCommand(command, state)`, `setAbsoluteVolume(volume)`, `onCommandResponse()`, `onAbsoluteVolumeChanged()`

## Notes

- **Only one A2DP role may be active.** Sink and Source cannot both run; `start()` on the second one fails.
- **AVRCP is a separate profile from A2DP.** Audio flows over A2DP, transport control over AVRCP; a peer may support one without the other.
- **Stream start / suspend arrives through `onStreamChanged()`**, which is what the Source's play / pause looks like from here.
- Target-side metadata and play-status responses are not implemented; the Controller here sends commands and observes responses.
- See the profile matrix for Arduino-ESP32 3.3.11 constraints: [docs/CLASSIC_PROFILE_SUPPORT.ja.md](../../../docs/CLASSIC_PROFILE_SUPPORT.ja.md).

## Expected Serial output

```
AVRCP Controller connected
Connected: 20:32:c6:1e:9d:4a, session=1
AVRCP response: command=68 state=1 accepted=1
Disconnected: session=1
```
