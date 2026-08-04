# MidiHost

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

Connects to a BLE MIDI peripheral as a central: scan the BLE MIDI service → connect → discover → subscribe → print decoded MIDI. Send a note from Serial. Pairs with the [MidiDevice](../MidiDevice/) example or a commercial BLE MIDI instrument.

## Hardware

- 1 × original ESP32 running this sketch (MIDI host / central)
- 1 × BLE MIDI peripheral: the [MidiDevice](../MidiDevice/) example or a commercial BLE MIDI instrument

## What it does

- Scans for the BLE MIDI service and connects to the first connectable match
- Discovers and subscribes to the MIDI I/O characteristic immediately on connect (no security in this example)
- Prints decoded MIDI, including SysEx chunks
- Sends middle-C Note On on Serial command `n`, then the Note Off from the write completion

## Two notes cannot be sent back to back

A central runs **one GATT operation at a time** here, so `sendNoteOff()` issued straight after `sendNoteOn()` would fail synchronously — the Note On write is still in flight. This example therefore sets `pendingNoteOff` and sends the Note Off from the write-completion event.

That event already has an observer: `EspBleMidiHost` uses it to drive multi-packet SysEx. The sketch adds its own with `bluetooth.addCharacteristicWrittenListener()` instead of `onCharacteristicWritten()`, so the helper keeps working while the application watches the same event.

## Key APIs

- `EspBleMidiHost midi(bluetooth)` — construct with a reference to the `EspBleBluedroid` instance
- `midi.begin()` — install host GATT callbacks; call after `bluetooth.begin()`
- `midi.discover(connectionId)` — discover and subscribe (call after connect / security)
- `midi.onMidiMessage(callback)` — decoded `EspBleMidiMessage` (status / data1 / data2 / timestamp)
- `midi.sendNoteOn(connectionId, ...)` / `sendNoteOff()` / `sendControlChange()` / `sendProgramChange()`
- `midi.sendSysEx(connectionId, data, length)` / `midi.sendingSysEx()` — split across writes, one per completion
- `midi.ready(connectionId)` — true once subscribed
- `bluetooth.addCharacteristicWrittenListener(callback)` — observe the completion the helper also uses

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Class, method and callback names | `EspBleMidiHost(EspBle &)` | identical apart from `EspBleMidiHost(EspBleBluedroid &)` |
| Sending while a GATT operation is in flight | may be issued | fails at once with `InvalidState`; send from the completion event |
| Connected MIDI devices | up to 4 | one central link |
| Wire format | BLE MIDI 1.0 packets from `EspBleMidi.h` | the same file, byte for byte (`tests/unit/midi`) |

**Why:** both differences belong to the GATT client, not to the MIDI helper: central operations are serialized and one central link is exposed while the direct-GATTC migration runs ([DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)). `sendSysEx()` already chains its own packets that way, so a long SysEx needs no change.

**How to port:** change the declaration of the library object, and chain consecutive sends through the write-completion event as this example does.

## Expected Serial output

```
MIDI: conn=1 status=0x90 data1=60 data2=100 ts=165
```
