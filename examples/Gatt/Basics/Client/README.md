# Client

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md)

Connects to the [Gatt/Basics/Server](../Server/) example and walks through the central GATT client flow: database enumeration → known-UUID discovery → read → writes with and without response → descriptor read/write → reading a value the server builds on demand. Each request returns `bool` immediately and completes later as an event from `bluetooth.update()`.

## Hardware

- 1 × original ESP32 running this sketch (central / GATT client)
- 1 × original ESP32 running the [Gatt/Basics/Server](../Server/) example

## What it does

- Scans for the server's service UUID and connects
- Enumerates services, characteristics, and descriptors into a connection-scoped snapshot
- Discovers the known characteristic, then chains read, acknowledged/unacknowledged writes, and descriptor read/write
- Finally reads `10da4dd3-…`, whose value the server produces in its `onRead()` callback, and prints it as `Live:`
- Demonstrates that only one central GATT operation runs at a time — the next operation is issued from the completion callback of the previous one

## Writing it as a chain

Every GATT operation is asynchronous, and **a central runs only one at a time**. Requesting a second while one is in flight fails synchronously with `InvalidState`. So the procedure cannot be written top to bottom; it becomes a **chain: request, then issue the next one from the completion event**.

This example is that chain, made visible:

```
onConnected        → discoverServices()
onServicesDiscovered → discoverCharacteristic()
onCharacteristicDiscovered → readCharacteristic()
onCharacteristicRead → writeCharacteristic()
onCharacteristicWritten → (unacknowledged write) → readDescriptor()
onDescriptorRead   → writeDescriptor()
onDescriptorWritten → discoverCharacteristic(live) → readCharacteristic(live)
```

**There is one event per kind of operation, not per target.** With several characteristics in play, results arrive at the same callback in turn, so identify the target with `result.characteristicUuid` — or by handle when the UUID cannot tell them apart. This example reads two characteristics, so `onCharacteristicRead` branches.

Enumeration results are held as a **per-connection snapshot**, valid until the connection drops or the next enumeration. `discoveredService*()` and friends query that snapshot without touching the radio.

## Key APIs

- `bluetooth.discoverServices()` / `onServicesDiscovered()` — enumerate the peer database
- `discoveredService*()` / `discoveredCharacteristic*()` / `discoveredDescriptor*()` — inspect the snapshot until disconnect or the next enumeration
- `bluetooth.discoverCharacteristic(connectionId, serviceUuid, characteristicUuid)` — known-UUID discovery
- `bluetooth.onCharacteristicDiscovered(callback)` — `EspBleGattResult` with `success`, properties, and `detail`
- `bluetooth.readCharacteristic(...)` / `bluetooth.onCharacteristicRead(callback)` — `result.value` holds the value (binary-safe)
- `bluetooth.writeCharacteristic(connectionId, serviceUuid, characteristicUuid, value, withResponse)` / `bluetooth.onCharacteristicWritten(callback)`
- `bluetooth.readDescriptor()` / `writeDescriptor()` and their completion callbacks
- Trailing `timeoutMilliseconds` on each operation (default 10000; zero is invalid) — expiration completes with `EspBleError::Timeout`
- Central GATT operations are exclusive: a second request while one is in flight fails synchronously with `InvalidState`

## Notes

- **A read returns the whole value, even above the MTU.** Bluedroid exposes no Read Blob call, so it would be reasonable to expect truncation at `mtu - 1` bytes; it continues the read internally instead and `result.value` holds the complete value. `tests/peer/long_value` pins this on hardware, because nothing in the API surface promises it.
- **Writes are not split.** A write goes out as a single ATT request; Long Write (writing across several requests) is not performed. The asymmetry with reads is because whether splitting works also depends on the peer's implementation. The limit for one request is MTU − 3 bytes, the same value as `maximumNotificationPayload()`.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Value longer than `mtu - 1` | read in pieces and joined automatically | identical result: the complete value arrives in `result.value` |
| Concurrent GATT operations | more than one may be issued | one at a time per connection |
| Discovery snapshot limits | 16 services / 48 characteristics / 48 descriptors | identical |

**Why:** the read path uses `esp_ble_gattc_read_char()` (and the wrapper's single-read fallback), neither of which performs a Read Blob sequence. Rather than silently pretending otherwise, the library returns exactly what one response carried.

**How to port:** keep values within `mtu - 1` bytes, negotiate a larger MTU, or split long payloads at the application level. The call itself is unchanged.

## Expected Serial output

The enumeration counts depend on the peer's GATT database (including the standard services the backend provides), and `Live:` is `millis()` at the moment of the read.

```
Services: ..., characteristics: ..., descriptors: ...
Read: ready
Descriptor: EspBleBluedroid value
Descriptor write complete
Live: 8421
```
