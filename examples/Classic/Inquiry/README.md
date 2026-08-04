# Inquiry

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [Bluetooth Classic beginner guide (Japanese)](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) — Inquiry
> EspBle: no counterpart — Bluetooth Classic only ([DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md))

Discovers nearby **discoverable Bluetooth Classic devices** and prints the address, name, RSSI, and Class of Device of each one.

Inquiry is the Classic counterpart of a BLE scan, and this library keeps the two **deliberately separate**: `bluetooth.classic().inquiry()` returns Classic fields, `bluetooth.scanner()` returns BLE advertising data. They are different operations with different result types, and running both at the same time is not guaranteed.

## Hardware

- 1 × original ESP32 running this sketch
- One or more discoverable Classic devices nearby — a phone with its Bluetooth settings screen open, a Classic headset in pairing mode, or a second board running [SppServer](../SppServer/)

A device that is *paired but not discoverable* does not answer an inquiry. That is a Classic property, not a bug: discoverability is a mode the peer enters deliberately.

## What it does

- Checks `capabilities().classicInquiry` **before** `begin()`, so a build without Classic support says so instead of failing later
- Initialises the shared Bluedroid stack with `begin()`
- Starts a 10-second inquiry
- Prints each result: address, plus name / RSSI / Class of Device when the peer provided them
- Prints the completion event, including whether it was cancelled

## Key APIs

- `bluetooth.capabilities()` — `EspBluedroidCapabilities::classicInquiry`, `classicSpp`, `classic`, `dualMode`, `ble`
- `bluetooth.classic().inquiry().start(config)` — `EspBluedroidClassicInquiryConfig::durationSeconds` (1–61) and `maxResponses` (0 = up to the backend limit)
- `bluetooth.classic().inquiry().stop()` — request cancellation; the later completion event reports `cancelled = true`
- `bluetooth.classic().inquiry().isRunning()` / `droppedResultCount()`
- `onResult(callback)` — `EspBluedroidClassicInquiryResult` with `address`, `name`, `rssi` / `hasRssi`, `classOfDevice` / `hasClassOfDevice`
- `onComplete(callback)` — `EspBluedroidClassicInquiryComplete::cancelled`

## Notes

- **Callbacks are delivered from `bluetooth.update()`**, not from the Bluedroid callback, and each result is a value-type copy. Keep calling `update()` or nothing arrives.
- **The result queue holds 16 entries.** Overflow is counted by `droppedResultCount()`.
- **`hasRssi` and `hasClassOfDevice` matter.** Not every response carries them; check the flag before reading the value.
- **A name may arrive empty.** Bluedroid resolves the friendly name in a second step, so a result can be reported with an address only.
- `stop()` may be called from inside `onResult()` — stopping as soon as the wanted device appears is the common pattern.

## Expected Serial output

```
20:32:c6:1e:9d:4a name=Pixel 8 RSSI=-54 CoD=0x5a020c
d0:cf:13:58:fd:95 name=EspBleBluedroid SPP RSSI=-38 CoD=0x1f00
Inquiry complete (cancelled=0)
```
