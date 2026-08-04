# ConnectionParameters

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 2, "GAP"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

Tunes a connection that is already established.

In BLE you **cannot choose the parameters that decide responsiveness and power draw when connecting**. The connection comes up with values the controller picked, and you request changes afterwards. That asymmetry is the confusing part, so this example prints what was decided at connect time before changing anything.

## The three parameters

| Parameter | Meaning | Unit |
|---|---|---|
| **Connection Interval** | How often the two sides get a chance to talk. Shorter is more responsive and costs more power | 1.25 ms |
| **Peripheral Latency** | How many of those chances the peripheral may skip when it has nothing to send | count |
| **Supervision Timeout** | Silence longer than this counts as a lost link | 10 ms |

The units are the raw spec units: `interval = 24` means 24 × 1.25 = 30 ms.

**The supervision timeout is constrained**: it must exceed `(1 + latency) × maxInterval × 2`. Raising the latency lets the peripheral stay quiet longer, and the timeout must not mistake that for a lost link. A request that violates this is rejected by the peer.

## Hardware

- 1 × original ESP32 running this sketch (central)
- A peripheral to connect to — the [Gap/Advertise](../Advertise/) example on a second board, or anything advertising the HID Service (`0x1812`)

## What it does

- Finds and connects to a peer advertising service UUID `0x1812`
- Prints the interval / latency / timeout **the controller chose** right after connecting
- `f` requests a low-latency profile (interval 15–30 ms, latency 0), `s` a low-power one (interval 400–500 ms, latency 4)
- `d` disconnects

## Key APIs

- `bluetooth.updateConnectionParameters(id, minInterval, maxInterval, latency, timeout)` — request a change
- `bluetooth.onConnectionParametersUpdated(callback)` — receive the negotiated result
- `EspBleConnection` — `connectionInterval` / `peripheralLatency` / `supervisionTimeout`

## Notes

- **The return value only says the request was accepted for sending.** Always read what actually happened from the callback: the peer may answer with different values, or reject the request.
- **Either role may request a change**, but the central's controller has the final say. A peripheral's request only takes effect once the central agrees.
- **Only a central connection can be tuned here.** `updateConnectionParameters()` takes the ID of a link this device opened with `connect()`. The values the peer chose are still visible in the connection snapshot on both sides.
- **The parameters cannot be requested at connect time.** `connect()` takes no parameter arguments; connect first, then change them from `onConnected()` if the defaults do not suit the application.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| PHY change | `updatePhy()` / `onPhyUpdated()` / `EspBleConnection::txPhy`, `rxPhy` | not available |
| Parameters at connect time | not available | not available |

**Why:** the original ESP32 radio implements Bluetooth 4.2 LE only, and has no LE 2M or LE Coded PHY. There is nothing to switch to, so the library does not expose `updatePhy()`, `onPhyUpdated()`, or the `txPhy` / `rxPhy` snapshot fields at all — rather than accepting a request that can never take effect.

**How to port:** delete the PHY branch and its callback. Everything else — the units, the supervision-timeout constraint, the request/result asymmetry — is identical to EspBle.

## Expected Serial output

```
Scanning for a peripheral...
CONNECTED interval=40 (50.00 ms) latency=0 timeout=256 (2560 ms)
Commands: f fast, s slow, d disconnect
REQUEST slow accepted=1
PARAMETERS interval=400 (500.00 ms) latency=4 timeout=600 (6000 ms)
REQUEST fast accepted=1
PARAMETERS interval=24 (30.00 ms) latency=0 timeout=400 (4000 ms)
```
