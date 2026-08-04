# NusClient

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md)

Central that talks to a Nordic UART Service (NUS) server by composing the generic GATT client API: it subscribes to TX notifications (`6e400003-…`) and sends each Serial line to RX (`6e400002-…`) using Write Without Response, under service `6e400001-…`.

## Hardware

- 1 × original ESP32 running this sketch (central / GATT client)
- 1 × original ESP32 running the [NusServer](../NusServer/) example (or any NUS-compatible peripheral)

## What it does

- Scans for the NUS service UUID and connects
- Subscribes to TX notifications after connecting and prints `NUS ready`
- Reads each non-empty line from Serial and writes it to RX (Write Without Response)
- Prints TX notifications received back from the server as `RX: …`

## Key APIs

- `bluetooth.subscribe(connectionId, serviceUuid, characteristicUuid)` — subscribe to TX notifications
- `bluetooth.onSubscribed(callback)` — subscription completion (`result.success`)
- `bluetooth.writeCharacteristic(connectionId, serviceUuid, characteristicUuid, value, false)` — Write Without Response to RX
- `bluetooth.onCharacteristicWritten(callback)` — write acceptance result
- `bluetooth.onNotification(callback)` — TX payload, filtered by `characteristicUuid.equalsIgnoreCase(...)`

## Notes

- This shows NUS built purely from the generic GATT client API; no stream semantics or automatic packet framing are implied. Type a line in the Serial monitor to send it.

## Expected Serial output

```
NUS ready: 1
TX accepted: 1
RX: hello
```
