# CustomClient

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

Reads a Custom HID device's arbitrary Report Descriptor and drives its reports using the **generic GATT client** (central). Pairs with the [CustomDevice](../CustomDevice/) example.

A HID device exposes several Report characteristics that all share UUID `0x2A4D`, so every attribute here is named by its distinct **attribute handle**. Each report's role is read from its own **Report Reference descriptor** (`0x2908`, one byte of report ID plus one byte of type: 1 = Input, 2 = Output, 3 = Feature) — which is how HID declares it. That descriptor is also addressed by handle: every Report Reference is `0x2908` under a `0x2A4D` characteristic, so a service/characteristic/descriptor UUID triple names all of them at once and none of them in particular.

If you only need a working HID host, [KeyboardHost](../KeyboardHost/) does all of this internally. This example is the manual version, for a device whose reports the library does not model.

## Hardware

- 1 × original ESP32 running this sketch (central / GATT client)
- 1 × original ESP32 running [CustomDevice](../CustomDevice/) (HID device / peripheral)

## What it does

- Actively scans and connects to a device advertising the HID service (`0x1812`)
- On connect, discovers services; when done, pairs each `0x2A4D` characteristic with its own `0x2908` descriptor. A descriptor belongs to one characteristic, and the link is the owning value handle, reported as `EspBleGattDescriptorInfo::characteristicHandle`
- Reads every Report Reference **by handle**, one at a time: each read is issued from the previous one's result, because this backend runs one central GATT operation per link
- Takes the role from the type byte: the Input report is subscribed to by handle, the Output report's handle is kept for writing
- Decodes the 2-byte input report (signed dial delta + buttons)
- Send `o` to write a 1-byte output report (`0x02`, LED state) by handle

## Key APIs

- `bluetooth.discoverServices(connectionId)` / `bluetooth.onServicesDiscovered(cb)` — trigger and receive GATT discovery
- `bluetooth.discoveredCharacteristicCount(connectionId, serviceUuid)` / `bluetooth.discoveredCharacteristic(connectionId, index, info, serviceUuid)` — enumerate characteristics; `EspBleGattCharacteristicInfo` carries `characteristicUuid`, `handle`, `notifiable`, `writable`
- `bluetooth.discoveredDescriptorCount(...)` / `bluetooth.discoveredDescriptor(...)` — enumerate descriptors; `EspBleGattDescriptorInfo` carries `descriptorUuid`, `handle`, and the owning `characteristicHandle`
- `bluetooth.readDescriptor(connectionId, descriptorHandle)` / `bluetooth.onDescriptorRead(cb)` — read a descriptor by attribute handle. In the result, `descriptorHandle` is the descriptor read and `handle` is the characteristic that owns it
- `bluetooth.subscribe(connectionId, handle, true)` — subscribe by attribute handle
- `bluetooth.onNotification(cb)` — `EspBleGattNotification` with the source `handle` and `value`
- `bluetooth.writeCharacteristic(connectionId, handle, data, length, response)` — write by handle

## Notes

- **One operation per link, so the sketch owns the sequencing.** `readNextReference()` issues the next read from the previous result and moves on to the subscription only when the reads are done. Issuing the next operation from inside a callback is fine: callbacks are dispatched from `bluetooth.update()`, not from inside the backend's own callback.
- **Advance on failure too.** A read that fails still has to move the cursor, or one unreadable descriptor stalls everything behind it.
- CustomDevice runs with security enabled, so a client without bonding may be rejected. Disable security on the device (or add bonding here) for a plain unencrypted demo.
- Discovered UUIDs come back in 128-bit form (`0000XXXX-...`); the sketch matches the 16-bit short form either way.
- The UUID form `readDescriptor(connectionId, serviceUuid, characteristicUuid, descriptorUuid)` exists as well, and is the right choice when the characteristic's UUID is unique. It cannot be used here: it would match whichever `0x2A4D` came first, which is not necessarily the report you meant.
- Reading the type rather than guessing from the properties matters because both an Output and a Feature report are writable. Properties still say something useful — only an Output report carries Write Without Response, since a Feature report is configuration and is always written with a response — but the type byte is what the device actually declares.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Class and method names | `ble.readDescriptor()` | identical |
| Issuing several reads | calls are queued automatically and run in order, so all of them can be issued from `onServicesDiscovered` | **one operation per link**: the second call fails while the first is running. Chain them — this sketch keeps the handles in an array and reads the next one from `onDescriptorRead` |
| Long descriptor / characteristic values | returned whole | identical; Bluedroid continues the read internally (`tests/peer/long_value`) |
| Simultaneous devices | several connections | one link at a time |

**How to port:** change the declaration of the library object, then turn any batch of GATT calls into a chain. That is the one change this example needed.

## Expected Serial output

```
Scanning for a Custom HID device. Send 'o' to write the output LED report.
Reading 2 Report Reference descriptors
Input report: id=1 handle=42
Output report: id=1 handle=45
Input report: dial delta=5 buttons=1
```
