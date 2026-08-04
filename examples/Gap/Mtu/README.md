# Mtu

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 2, "GAP"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

Requests a larger ATT MTU before connecting and observes the negotiated value. The preferred MTU is set in the config passed to `begin()`, and the library exchanges it right after the link comes up.

## Hardware

- 1 × original ESP32 running this sketch (central)
- 1 × BLE peripheral — the sketch scans for the service UUID of the [Gatt/Basics/NotifyServer](../../Gatt/Basics/NotifyServer/) example, so run that on a second board

## What it does

- Sets `config.preferredMtu = 185` before `begin()`
- Connects to the first result advertising the NotifyServer service UUID
- Prints the negotiated MTU and the resulting maximum notification payload (`mtu - 3`)
- Prints MTU-change events with the previous and new value

## Key APIs

- `EspBleConfig::preferredMtu` — desired ATT MTU (23–517); out-of-range values are rejected by `begin()` with `InvalidArgument`
- `connection.mtu` — the MTU as of that event. **It is still 23 right after connecting**: the exchange happens just after the connection comes up, so `onConnected` sees the default and the negotiated value arrives through `onMtuChanged` (same order on both roles)
- `connection.maximumNotificationPayload()` — `mtu - 3` (ATT notification header)
- `bluetooth.onMtuChanged(callback)` — `event.previousMtu` and `event.connection.mtu`

## Notes

- **The exchange is started from `update()`, not from inside a stack callback.** Bluedroid does not accept an MTU request from within its own connection callback, so the library defers the single request to the next `update()` after the connect worker finishes. A sketch that stops calling `update()` after connecting therefore stays at MTU 23.
- The negotiated value is written into the connection snapshot at the same time `onMtuChanged()` is delivered, so `bluetooth.connection(id, out)` afterwards reports the agreed MTU.
- A `disconnect()` issued while the exchange is in flight is accepted and deferred until the exchange completes.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| `EspBleConfig::preferredMtu` default | 247 | 247 |
| When the exchange starts | during connection establishment, inside the backend | from the first `update()` after the connect worker completes |
| MTU in `onConnected()` | 23 (default) | 23 (default) |

**Why:** Bluedroid rejects `esp_ble_gattc_send_mtu_req()` when it is called from its own GATTC callback context, so the request cannot be issued at the moment the link comes up. EspBleBluedroid queues exactly one request per link and issues it from `update()` instead.

**How to port:** no code change. The sketch is the same as the EspBle one; only the timing of `onMtuChanged()` shifts by one `update()` cycle.

## Expected Serial output

```
Connected with MTU 23 (notification payload up to 20 bytes)
MTU changed from 23 to 185
```
