# BondManagementClient

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md)

Connects to a Bond Management Service (0x181E), reads the Bond Management Feature bit field, and writes the Bond Management Control Point op code "Delete bond of requesting device (LE)" (0x03) with response.

## Hardware

- 1 × original ESP32 running this sketch (central)
- 1 × Bond Management peripheral: the [BondManagementServer](../BondManagementServer/) example

## What it does

- Scans for and connects to a device advertising 0x181E
- Reads the Bond Management Feature (0x2AA5) and prints the supported-operations bit field
- Writes the Bond Management Control Point (0x2AA4) op code 0x03 (Delete bond of requesting device, LE) **with response**

## Key APIs

- `bluetooth.writeCharacteristic(connectionId, service, characteristic, data, length, true)` — write with response

## Notes

- **The op code says what to delete, and the Feature bit field says what the server can do.** Read the Feature first: a server that does not advertise "delete bond of requesting device" cannot honour 0x03. The [BondManagementServer](../BondManagementServer/) example here advertises `0x000400` ("delete all bonds on server, LE") and answers 0x03 by clearing every LE bond, because a peripheral on this library cannot identify the requesting peer.
- A write with response tells you the server accepted the op code, not that the bond is already gone; the removal is usually deferred until after the disconnect.

## Expected Serial output

```
Bond Management Feature: 0x000400
Delete-bond op code sent
```
