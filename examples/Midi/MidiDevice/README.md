# MidiDevice

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

Advertises a BLE MIDI peripheral using the standard BLE MIDI service. Send Note On/Off from Serial and print any MIDI a connected host sends back. Pairs with the [MidiHost](../MidiHost/) example or any BLE MIDI host (phone/tablet DAW).

## Hardware

- 1 × original ESP32 running this sketch (MIDI device / peripheral)
- 1 × BLE MIDI host: a smartphone/tablet DAW, a computer, or a second board running the [MidiHost](../MidiHost/) example

## What it does

- Registers the BLE MIDI service and its I/O characteristic before `begin()` (the service UUID is added to advertising)
- Sends middle-C Note On then Note Off on Serial command `n` (only while a host is subscribed)
- Prints MIDI received from the host (host → device), including SysEx chunks

## The helper does not take your callbacks

`EspBleMidiDevice` needs the GATT Server's written / subscription-changed / sent events, but it registers them with `add*Listener()` rather than the single `on*()` primary. A sketch can still install its own `onWritten()` or another `addWrittenListener()` for the same events — the helper and the application observe the same event side by side. `tests/peer/multi_listener` pins that ordering on hardware.

## Key APIs

- `EspBleMidiDevice midi(bluetooth)` — construct with a reference to the `EspBleBluedroid` instance
- `midi.begin()` — register the service; call before `bluetooth.begin()`
- `midi.noteOn(channel, note, velocity)` / `midi.noteOff(...)` — send channel-voice messages
- `midi.controlChange()` / `programChange()` / `polyPressure()` / `channelPressure()` / `pitchBend()`
- `midi.sendSysEx(data, length)` / `midi.sendingSysEx()` — a long SysEx is split across packets and sent one per `onSent`
- `midi.onMessage(callback)` — MIDI received from the host, decoded into `EspBleMidiMessage`
- `midi.ready()` — true while a host is subscribed

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Class, method and callback names | `EspBleMidiDevice(EspBle &)` | identical apart from `EspBleMidiDevice(EspBleBluedroid &)` |
| Subscribed hosts | up to 4 | one — the GATT Server exposes a single peripheral link |
| Wire format | BLE MIDI 1.0 packets from `EspBleMidi.h` | the same file, byte for byte (`tests/unit/midi`) |

**Why:** `src/EspBleMidiProfile.h` is EspBle's file with the library reference retyped; diff the two and the substitution is all there is. The subscriber limit is the Bluedroid GATT Server's, not the helper's — `MaxSubscribers` is still 4 in the table.

**How to port:** change the declaration of the library object. Nothing else in a MIDI sketch changes.

## Expected Serial output

```
MIDI in: status=0xb0 data1=7 data2=100 ts=1234
SysEx chunk: start=1 end=0 length=16
```
