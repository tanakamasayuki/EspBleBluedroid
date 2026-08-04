# HfpAudioGateway

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [Bluetooth Classic beginner guide (Japanese)](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) — HFP
> EspBle: no counterpart — Bluetooth Classic only ([DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md))

The other half of HFP: this board plays the **phone's** role. It waits for a headset to connect and carries mono call audio in both directions.

Use it to bring up a headset without a phone in the loop — pair it with [HfpHandsFree](../HfpHandsFree/) on a second board and both sides are code you control.

## Hardware

- 1 × original ESP32 running this sketch (HFP Audio Gateway / phone role)
- 1 × Hands-Free device — a Bluetooth headset, or a second board running [HfpHandsFree](../HfpHandsFree/)

## What it does

- Starts the Audio Gateway role, which makes Classic **connectable and discoverable** — no `connect()` call is needed here
- Prints the peer address when a headset establishes the SLC
- Receives the headset's microphone audio in `onPcmData()`
- Supplies downlink call audio in `onPcmRequested()` — silence in this example

## Direction of audio, from this side

| Callback | Direction | Real-world equivalent |
|---|---|---|
| `onPcmData()` | headset → this board | what the user says into the microphone |
| `onPcmRequested()` | this board → headset | what the caller on the other end says |

Both run **synchronously on the HFP stack task**: copy through a bounded queue,
never block, and set `request.written` in the request callback. Mono 16-bit
samples; read `request.format` for the negotiated rate (8 kHz CVSD / 16 kHz mSBC).

## Key APIs

- `bluetooth.classic().hfpAudioGateway()` — `start()`, `stop()`, `connect(peerAddress)`, `disconnect(sessionId)`, `connectAudio(sessionId)`, `disconnectAudio(sessionId)`, `session(out)`
- `onConnected()` / `onDisconnected()` / `onStarted()` / `onConnectionFailed()` / `onAudioChanged()` — delivered from `update()`
- `onPcmData()` / `onPcmRequested()` — the stack-task audio callbacks
- `EspBluedroidHfpSession` — `incoming` is `true` on this path

## Notes

- **Starting the role makes the device discoverable.** That is what lets a headset find and connect to it, and it is the main behavioural difference from the Hands-Free side.
- **Either side may bring SCO up.** `connectAudio()` exists here too; `onAudioChanged()` reports the state whoever initiated it.
- **Call indicators and call control are still being implemented**, so this example carries audio without signalling ringing or an active call.
- Only one HFP session at a time.

## Expected Serial output

```
HF connected: 41:42:d8:70:1c:33
```
