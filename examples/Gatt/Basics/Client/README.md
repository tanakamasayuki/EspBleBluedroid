# Client

> 日本語版: [README.ja.md](README.ja.md)

Connects to a compatible custom GATT Server and runs database discovery,
Characteristic Read, acknowledged and unacknowledged Writes, and Descriptor
Read/Write.

The peer must advertise Service
`10da4dd0-8eaa-4c69-9003-676174747277`, expose a readable/writable
Characteristic ending in `...dd1...`, and a readable/writable Descriptor
ending in `...dd2...`. A generic GATT Server application or another firmware
can provide this database.

## Requirements

- One original ESP32 running this Central sketch
- A compatible Peripheral providing the database described above

## Behavior

- Discovers the database and selects the known Characteristic handle
- Runs Read, both Write modes, and Descriptor Read/Write in order

## Main APIs

- `discoverServices()` and the connection-scoped discovery snapshot
- Handle-based Characteristic Read/Write
- Descriptor Read/Write and completion callbacks

Only one Central GATT operation runs at a time. Each next request is therefore
issued from the preceding completion callback delivered by `update()`.

## Expected Serial output

```text
Read: ready
Descriptor: value description
Descriptor write complete
```
