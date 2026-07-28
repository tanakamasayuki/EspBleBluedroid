# ConnectionParameters

> 日本語版: [README.ja.md](README.ja.md)

Prints and updates the three parameters controlling responsiveness and power
usage on a live BLE connection.

The controllers negotiate these values. An accepted request does not guarantee
that the peer will select the requested values, so this example prints the
initial snapshot and observes the negotiated result through a callback.

## Parameters

| Parameter | Meaning | Unit |
|---|---|---|
| Connection Interval | Time between connection events | 1.25 ms |
| Peripheral Latency | Events the Peripheral may skip | event count |
| Supervision Timeout | Silence before the link is considered lost | 10 ms |

Supervision Timeout must be longer than
`(1 + latency) × maxInterval × 2`. This prevents permitted Peripheral silence
from being mistaken for a lost link.

## Requirements

- One original ESP32 running this Central sketch
- A second board running [Gap/Advertise](../Advertise/), or another connectable
  Peripheral advertising Battery Service (`0x180F`)

## Behavior

- Prints the controller-selected values after connection
- `f` requests a low-latency profile
- `s` requests a lower-power profile
- `d` disconnects

## Main APIs

- `EspBleConnection::connectionInterval`
- `EspBleConnection::peripheralLatency`
- `EspBleConnection::supervisionTimeout`
- `updateConnectionParameters(id, min, max, latency, timeout)`
- `onConnectionParametersUpdated(callback)`

## Notes

- Request acceptance does not guarantee the requested values. Always inspect
  the snapshot delivered by the completion callback.
- The callback also updates the snapshot returned by `connection()`.
- This API currently applies to Central connections.
- Original ESP32 controllers support only the 1M PHY, so this example does not
  include PHY switching.

## Expected Serial output

```text
CONNECTED interval=24 (30.00 ms) latency=0 timeout=400 (4000 ms)
REQUEST slow accepted=1
PARAMETERS interval=400 (500.00 ms) latency=4 timeout=600 (6000 ms)
```
