# Server

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md)

Registers a custom GATT service with one readable/writable characteristic and descriptor, then advertises it. The characteristic supports writes with and without response. It also carries one characteristic whose **value is produced at the moment it is read**.

Use the [Gatt/Basics/Client](../Client/) example on a second board (it targets the same UUIDs), or any GATT client app such as nRF Connect.

## Hardware

- 1 × original ESP32 running this sketch (peripheral / GATT server)
- 1 × GATT client (second board running Gatt/Basics/Client, or a smartphone app)

## What it does

- Adds service `10da4dd0-…`, characteristic `10da4dd1-…`, descriptor `10da4dd2-…`, and the read-only `10da4dd3-…` before `begin()`
- Sets the initial value to `ready`
- Prints each write received from a client, together with the connection ID
- Answers a read of `10da4dd3-…` with `millis()` taken at that moment
- Advertises the service UUID so clients can find it

## Building the server from handles

Registration is a **three-step handle chain**: the handle from `addService()` goes into `addCharacteristic()`, whose handle goes into `addDescriptor()`.

```cpp
const EspBleGattService service = gattServer.addService(SERVICE_UUID);
characteristic = gattServer.addCharacteristic(service, CHARACTERISTIC_UUID, valueConfig);
descriptor = gattServer.addDescriptor(characteristic, DESCRIPTOR_UUID, descriptorConfig);
```

Every later value, send, and event check uses those handles rather than UUIDs, because **a UUID is a type, not an identity**. The spec lets one device expose several services with the same UUID, and from the client side a peer with several same-UUID characteristics (HID Reports, for example) is entirely normal.

Keep the handles in globals. A failed registration returns an invalid handle, which `valid()` reports.

## Producing a value when it is read

Storing the value ahead of time with `setValue()` suits data this device already knows has changed. For a sensor-style value that should reflect the moment of the read, use `onRead()`.

```cpp
gattServer.onRead([](const EspBleGattReadRequest &request) {
  if (request.characteristic != liveCharacteristic) return;
  bluetooth.gattServer().setValue(liveCharacteristic, String(millis()));
});
```

Whatever the callback passes to `setValue()` is what the peer receives. No periodic `setValue()` loop is needed, and **if nobody reads it, the work of producing the value never runs**.

**This one callback runs on the BLE stack task, not from `update()`.** The value has to exist before the ATT read response goes out, so there is nowhere to defer it to. Two consequences:

- **Keep it short.** Blocking here stalls the whole stack, and the peer sees the read time out. Avoid serial output inside it
- **It runs concurrently with `loop()`.** Unlike every other callback, touching shared state here needs synchronisation

## Key APIs

- `bluetooth.gattServer().addService(uuid)` — register a service and return its handle; must be called before `begin()`
- `addCharacteristic(service, uuid, config)` — register a characteristic in that service and return its handle
- `EspBleGattCharacteristicConfig` — `readable`, `writable`, plus `notifiable`, `indicatable`, and encrypted/authenticated permissions
- `addDescriptor(characteristic, uuid, config)` / `EspBleGattDescriptorConfig` / `setDescriptorValue(descriptor, value)` — descriptor definition, permissions, and binary-safe value
- `gattServer.setValue(characteristic, value)` / `gattServer.value(characteristic, out)` — held value (binary-safe `String`, pointer+length overloads available)
- `gattServer.onWritten(callback)` — `EspBleGattWrite` with `connectionId`, the handle of the characteristic written, and the value
- `gattServer.onRead(callback)` — a read request; `EspBleGattReadRequest` carries `connectionId` and the target handle
- `gattServer.onDescriptorWritten(callback)` — `EspBleGattDescriptorWrite` with the descriptor handle and value

## Notes

- **One callback serves every characteristic.** With more than one registered, check `write.characteristic == myHandle`. The event also carries UUID strings, but those cannot tell apart characteristics that share a UUID, so comparing handles is the reliable test.
- **Two characteristics with the same UUID in one service are fine**, as the spec permits (HID Reports are the everyday case): each `addCharacteristic()` returns its own handle, and every later operation takes that handle, so the pair is never ambiguous. `tests/peer/duplicate_uuid_server` reads both back from a peer. **Two descriptors with the same UUID under one characteristic are not**: a descriptor is looked up by UUID inside its characteristic, so the second one would be unreachable and `addDescriptor()` fails with `InvalidArgument`.
- **There is one callback per event, not a list.** `onWritten()`, `onRead()`, `onSubscriptionChanged()`, and `onSent()` each hold a single callback; calling one again replaces the previous one. Dispatch to several parts of the application from inside your own callback.
- **A value larger than one ATT response is read across several responses.** The client asks for the rest, and this library's own GATT client gets the whole value too ([Gatt/Basics/Client](../Client/)). A peer that does not continue the read sees only the first `mtu - 1` bytes, which is the client's decision, not the server's.
- All registration must happen before `begin()`; `addService()` afterwards fails with `InvalidState`.
- **The database is bounded**: 8 services, 32 characteristics, 16 descriptors. Exceeding a limit fails the registration call with `ResourceExhausted`.
- **Advertising does not resume by itself after a peer disconnects.** Bluedroid stops advertising on connection, and there is no peripheral disconnect event here, so this server accepts one connection per boot unless the sketch calls `advertising().start()` again — see [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md) for the ways to do that.
- **`write.connectionId` identifies the peripheral link the event came from.** It is a GATT-server-side ID; `bluetooth.connection(id, out)` describes central links only, so do not mix the two.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Database limits | 8 services / 32 characteristics / 16 descriptors | identical |
| Duplicate UUIDs in one service | allowed | **rejected at registration** |
| Multiple listeners per event | `add*Listener()` (up to `MaxListenersPerEvent`) | one callback per event |
| `onRead()` context | BLE stack task | BLE stack task (identical) |
| Peripheral connection snapshot | `bluetooth.connection(id, out)` works for incoming links | **not available**; use `write.connectionId` and the GATT-server events |

**Why:** the GATT server still builds its attribute table through the Arduino-ESP32 Bluedroid wrapper, whose per-service lookup is by UUID — so a second characteristic with the same UUID would be unaddressable. The library rejects it at registration time instead of failing later at run time. Single callbacks and the missing peripheral snapshot are current scope, tracked in [docs/STATUS.ja.md](../../../../docs/STATUS.ja.md).

**How to port:** give same-UUID characteristics distinct UUIDs, and replace `add*Listener()` calls with one callback that fans out. `addService()` / `addCharacteristic()` / `addDescriptor()` / `setValue()` / `notify()` / `indicate()` are used exactly as in EspBle.

## Expected Serial output

```
Connection 1 wrote: hello from Central
Descriptor 10da4dd2-8eaa-4c69-9003-676174747277 wrote: descriptor value
```
