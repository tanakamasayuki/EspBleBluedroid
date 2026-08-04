# ProfileSupport

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [Bluetooth Classic beginner guide (Japanese)](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) — Capability
> EspBle: no counterpart — Bluetooth Classic only ([DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md))

Prints, for each major Bluetooth Classic profile, **whether it can be used and why** — without initializing the Bluetooth stack at all.

"Can I use A2DP here?" has more than one possible answer, and they need different responses from you: a profile can be missing because this library has not implemented it, because the Arduino-ESP32 build disabled it, because ESP-IDF exposes no public API for it, or because no standard Classic profile exists in the first place. This example reports which of those applies.

## Hardware

- 1 × original ESP32 running this sketch; no peer, and no Bluetooth traffic at all

## What it does

- Walks a table of profiles (SPP, A2DP Sink / Source, AVRCP Controller / Target, HID Device / Host, HFP Hands-Free / Audio Gateway, PBAP Client, MIDI)
- Calls `bluetooth.classic().profileSupport(profile)` for each one **before `begin()`**
- Prints the status name and the human-readable reason string

## The five statuses

| Status | Meaning | What to do |
|---|---|---|
| `Supported` | Available in both the Core and EspBleBluedroid | Use it |
| `LibraryNotImplemented` | The Core has it; this library has no public API yet | Track [docs/CLASSIC_PROFILE_SUPPORT.ja.md](../../../docs/CLASSIC_PROFILE_SUPPORT.ja.md), or drive ESP-IDF directly |
| `CoreDisabled` | Turned off by an Arduino-ESP32 build option | Needs a custom Core build; not switchable at run time |
| `CoreApiUnavailable` | ESP-IDF exposes no usable public profile API | Nothing to call, whatever the library does |
| `NoStandardProfile` | No standard Classic profile exists | Use a profile that does, or carry the data over SPP |

## Key APIs

- `bluetooth.classic().profileSupport(profile)` → `EspBluedroidClassicProfileSupport` with `status`, `coreAvailable`, `implemented`, and `reason`
- `EspBluedroidClassicProfile` — the profile enumeration (`Spp`, `A2dpSink`, `A2dpSource`, `AvrcpController`, `AvrcpTarget`, `HidDevice`, `HidHost`, `HfpHandsFree`, `HfpAudioGateway`, `Hsp`, `Pan`, `PbapClient`, `PbapServer`, `Map`, `Opp`, `Ftp`, `Dun`, `Sap`, `Midi`, `Gap`)
- `bluetooth.capabilities()` — the coarser device-level snapshot (`classic`, `classicInquiry`, `classicSpp`, `dualMode`, `ble`)

## Notes

- **This is a compile-time snapshot.** It reflects the Core build options and this library's implementation state, so the answer never changes at run time — which is why calling it before `begin()` is fine and cheap.
- **Gamepads live under HID.** Check `HidDevice` / `HidHost`. With the stock Arduino-ESP32 3.3.11 build these report `CoreDisabled`, because `CONFIG_BT_HID_ENABLED` is off.
- **MIDI over Classic reports `NoStandardProfile`.** Classic has no standard MIDI profile; MIDI over Bluetooth is a BLE GATT service, and this library does not implement it (EspBle does).
- Use this example first when a Classic profile "does not work" — it separates "not built" from "not implemented" in one run.

## Expected Serial output

```
SPP: supported
  SPP Server and Client APIs are available
A2DP Sink: supported
  A2DP Sink and Source SBC/PCM APIs are available; only one A2DP role may be active
AVRCP Controller: supported
  AVRCP Controller and Target passthrough and absolute-volume APIs are available
HID Device / GamePad: core-disabled
  CONFIG_BT_HID_ENABLED is disabled by the Core build
HFP Hands-Free: library-not-implemented
  HFP Hands-Free SLC, SCO, and built-in-codec PCM APIs are available; call control is still being implemented
```

(The HFP entries report `library-not-implemented` because call control is still
being built out; the SLC / SCO / PCM APIs those examples use do work — see
[HfpHandsFree](../HfpHandsFree/).)
