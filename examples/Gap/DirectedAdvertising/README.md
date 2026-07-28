# DirectedAdvertising

> 日本語版: [README.ja.md](README.ja.md)

A peripheral-side example that advertises connectability directly to one known central.

Normal advertising invites every nearby central. Directed Advertising puts a destination address in the packet, and controllers other than that destination discard it. It is useful for quickly reconnecting to a bonded device.

## Hardware

- 1 × original ESP32 running this sketch
- The target central

Replace `TARGET_CENTRAL` with the central's identity address and select its matching `Public` or `Random` address type. Another original ESP32 can report its public address with `bluetooth.localAddress()`.

## High Duty and Low Duty

| Mode | Interval | Lifetime | Typical use |
|---|---:|---:|---|
| `HighDutyCycle` | Fixed 3.75 ms | At most 1.28 seconds | Fast reconnection immediately after a disconnect |
| `LowDutyCycle` | `setInterval()`, or 1.28 seconds by default | Until `stop()` or connection | Lower-power reconnection waiting |

The controller stops High Duty automatically when the target does not connect. Keep calling `bluetooth.update()` so `isAdvertising()` also reflects that stopped state.

## Important restrictions

The BLE specification permits no Local Name, Service UUID, Manufacturer Data, other AD data, or Scan Response in Directed Advertising. `startDirected()` therefore returns `InvalidState` when `data()` or `scanResponse()` contains a value.

Call `stop()` before switching from an active normal advertising operation. `startDirected()` does not silently replace another active operation.

For a bonded peer using RPA, specify its identity address and correct identity address type, not a temporary observed RPA.

Directed Advertising limits who may connect; it does not provide encryption or authentication after connection. Configure BLE Security when values must be protected.

## Controls

- `h` — restart in High Duty mode
- `l` — restart in Low Duty mode
- `x` — stop

## Main API

```cpp
bluetooth.advertising().startDirected(
  targetAddress,
  EspBleAddressType::Public,
  EspBleDirectedAdvertisingMode::HighDutyCycle);
```
