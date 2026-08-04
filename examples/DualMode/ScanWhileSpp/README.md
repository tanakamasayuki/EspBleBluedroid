# ScanWhileSpp

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [Bluetooth Classic beginner guide (Japanese)](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) — dual mode
> EspBle: no counterpart — needs Bluetooth Classic ([DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md))

Runs **BLE and Bluetooth Classic at the same time**: connect an SPP session, then perform an active BLE scan without dropping it.

One `EspBleBluedroid` object owns one dual-mode Bluedroid stack, but the two transports stay separate APIs with separate result types. BLE lives at the root (`scanner()`, `connect()`, `gattServer()`), Classic lives under `classic()`. Nothing merges, and that is deliberate — a BLE scan result and a Classic inquiry result are not the same thing.

## Hardware

- 1 × original ESP32 running this sketch (SPP client **and** BLE central)
- 1 × SPP server — a second board running [Classic/SppServer](../../Classic/SppServer/), or a phone serial terminal
- Optional: any BLE advertiser nearby, such as [Gap/Advertise](../../Gap/Advertise/), so the scan has something to report

## What it does

- Prompts for a Classic address and calls `classic().spp().connect()`
- On `onConnected()`, remembers the SPP session ID and starts a **ten-second active BLE scan**
- Prints each BLE scan result together with the still-active SPP session ID — the proof both transports are live
- Stops the scan when the SPP session drops
- Prints SPP connection failures with their detail string

## Key APIs

- `bluetooth.classic().spp()` — `connect()`, `onConnected()`, `onDisconnected()`, `onConnectionFailed()`, `write()`
- `bluetooth.scanner()` — `start(config)`, `stop()`, `isScanning()`, `onResult()`
- `bluetooth.update()` — the single pump for **both** event paths
- `bluetooth.capabilities()` — `dualMode` reports whether this build can do it at all

## What is verified on hardware

The two-board peer test behind this example goes further than the sketch:

- Active BLE scanning, a BLE central connection, GATT discovery, read / write, and notifications, all while an SPP session carries binary data
- Notification bursts of 64, then 128, then 256 on the same connection and subscription, with per-round accounting: delivered notifications, `droppedEventCount()` increments, SPP round-trip bytes, and receive-ring packet counts all reconcile
- When the BLE event queue fills, **control events win**: connect / security / GATT-completion events are kept and the oldest notification is dropped, so a saturated queue never loses a completion

Long-duration soak behaviour and fairness under continuous saturation are not yet verified.

## Notes

- **Keep calling `update()`.** Both BLE and Classic callbacks are delivered from it; blocking `loop()` starves both.
- **Classic Inquiry and BLE scanning are different operations.** Running them simultaneously is not guaranteed — this example pairs an SPP *session* with a BLE scan, not an inquiry with a scan.
- **The BLE event queue is bounded.** Under a notification burst, `bluetooth.droppedEventCount()` is how you observe loss explicitly rather than silently.
- One SPP session and one BLE central connection at a time.

## Expected Serial output

```
Enter the Classic address of an SPP Server
BLE 5a:b8:1e:0c:2f:71 RSSI=-52 while SPP session 1 is active
BLE 70:04:1d:32:99:a0 RSSI=-78 while SPP session 1 is active
```
