# KeyboardHost

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

Connects to a BLE keyboard as a HID host (central): scans for the HID service `0x1812`, pairs, discovers the HID reports, and prints key events. The single `hidHost()` object also dispatches mouse, consumer-control, system-control, and gamepad reports, so it works with commercial BLE keyboards and with the [KeyboardDevice](../KeyboardDevice/), [KeyboardNkro](../KeyboardNkro/) or [CompositeKeyboardMouse](../CompositeKeyboardMouse/) example on a second board.

## Hardware

- 1 × original ESP32 running this sketch (HID host / central)
- 1 × BLE HID device: a commercial keyboard, or a second board running KeyboardDevice / KeyboardNkro / CompositeKeyboardMouse

## What it does

- Scans and connects to the first connectable device advertising the HID service `1812`
- Enables security with bonding and starts HID discovery **after** `onSecurityChanged` reports success — commercial keyboards reject HID-attribute access before encryption
- Sets the keyboard layout to EN-US, then prints the discovery result (report ID, battery), the raw usage state snapshot, and per-key press events with the layout-converted ASCII value
- Also prints mouse, consumer-control, system-control and gamepad events from the same `hidHost()` object
- Send `c` to set the Caps Lock LED, `0` to clear all LEDs (only while connected)

## Key APIs

- `bluetooth.hidHost().discover(connectionId)` — explicit HID discovery; re-call with the new connection ID after each reconnect, or turn on `setAutoRediscover(true)`
- `keyboard.onDiscovered(cb)` — `EspBleHidKeyboardHostDiscovery` with `success`, `reportId`, `hasCountryCode` / `countryCode`, `hasOutputReport`, `hasBatteryLevel` / `batteryLevel`, `detail`
- `keyboard.onKeyboardState(cb)` — layout-independent 256-bit usage snapshot (`isDown()`, `wasPressed()`, `wasReleased()`, `modifiers`)
- `keyboard.setKeyboardLayout(EspBleKeyboardLayout::EnUs)` / `keyboard.onKeyboard(cb)` — per-usage press/release events; `ascii` is nonzero only when convertible
- `keyboard.onMouse()` / `onConsumerControl()` / `onSystemControl()` / `onGamepad()` / `onVendorInput()` — typed composite-HID events
- `keyboard.setKeyboardLeds(connectionId, num, caps, scroll)` — fire-and-forget LED write (Write Without Response)
- `keyboard.ready(connectionId)` — discovery finished and this link's input reports are subscribed
- `keyboard.invalidInputReportCount()` / `droppedEventCount()` — reports the host could not use, and events that overran the queue

## Notes

- **Discovery is a sequence, not one call.** This backend allows one central GATT operation per link, so `discover()` walks the device a step at a time: services, the Report Map, every Report Reference descriptor, HID Information, Battery Level, then a subscription per input report. `discover()` returning true only means the sequence started; wait for `onDiscovered()`, and do not issue your own GATT operations on that link until it arrives.
- **Read `result.detail` on failure, not `lastErrorName()`.** `onDiscovered()` runs after the sequence has finished, by which time the library's last error has moved on to whatever happened next.
- **A modifier is both a bit and a usage.** Shift+A arrives as `event.modifiers = 0x02` on the `A` event *and* as an event of its own for usage `0xe1` with `ascii = 0`. `isDown(0xe1)` answers for it too.
- **The state event comes first.** For one incoming report, `onKeyboardState()` runs before the per-usage `onKeyboard()` events, so a sketch can read the whole snapshot before the edges are reported.
- Discovery, state and key events are all delivered from `bluetooth.update()`.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Class, method and callback names | `ble.hidHost()` | identical |
| Discovery | issued as a sequence of GATT operations | identical in API; internally one operation at a time, because this backend allows one central GATT operation per link |
| `event.rawData` / `rawLength` on a keyboard event | left empty | carries the report the event was decoded from (`length` 8 for a 6KRO keyboard). Requested for EspBle in [docs/ESPBLE_FEEDBACK.ja.md](../../../docs/ESPBLE_FEEDBACK.ja.md) |
| Hands-off reconnection | `setAutoReconnect(true)` + `persistentSubscriptions` + `setAutoRediscover(true)` | `setAutoRediscover(true)` exists and re-runs discovery after a known peer reconnects and re-encrypts, but **reconnecting is the sketch's job**: there is no `setAutoReconnect()` here ([Gatt/Basics/AutoReconnectClient](../../Gatt/Basics/AutoReconnectClient/) shows the manual pattern) |
| Simultaneous keyboards | several connections | one link at a time |

**How to port:** change the declaration of the library object. If the sketch relied on `setAutoReconnect()`, add the reconnect loop from [Gatt/Basics/AutoReconnectClient](../../Gatt/Basics/AutoReconnectClient/); `setAutoRediscover(true)` then covers the HID half.

## Expected Serial output

```
Keyboard ready: report=1 battery=73%
Keyboard state: modifiers=0x02 A=1 pressed=1 released=0
Key pressed: usage=0x04 ascii=0x41
Key pressed: usage=0xe1 ascii=0x00
```
