# EspBleBluedroid

EspBleBluedroid is an Arduino library for ESP32 boards using the Bluedroid
stack. It aims to provide an experience similar to its NimBLE-based sibling
library, [EspBle](https://github.com/tanakamasayuki/EspBle), especially for GATT operations after a
connection is established.

The project is in its initial bring-up stage. Its public API currently covers
the root lifecycle, legacy advertising, scanning, one Central connection, and
asynchronous GATT discovery, UUID/handle-based Characteristic operations,
Descriptor Read/Write, and Notification subscription,
verified by automated two-board peer tests on original ESP32 boards. Just Works
security, static-passkey and runtime-passkey MITM, Numeric Comparison, and BLE
bond management are also available. The first Bluetooth Classic API provides a
capability snapshot, asynchronous Inquiry, and binary-safe SPP Client/Server
sessions under `classic()`, including the root-bound `EspBluedroidSppSerial`
that automatically follows active Server or Client sessions, deferred
write-completion results, and SSP Numeric
Comparison for authenticated/encrypted SPP. Classic DisplayOnly/KeyboardOnly
Passkey Entry is also available with
deferred display/request callbacks. Classic link keys can be listed and deleted through a bond API kept
separate from BLE bonds, and secure SPP is covered in both Client and Server
roles. Additional Classic profile behavior will continue to be added
test-first.

See [tests/README.md](tests/README.md) for setup and usage.

The current API and Bluetooth Classic coexistence policy is documented in
[Japanese](docs/API_DESIGN_POLICY.ja.md).
Japanese introductory guides cover
[BLE](docs/GUIDE_BLE_BASICS.ja.md) and
[Bluetooth Classic](docs/GUIDE_CLASSIC_BASICS.ja.md) separately.

The implemented and planned feature set is tracked in
[Japanese](docs/STATUS.ja.md), with examples in [examples](examples/README.md).

Implementation follows the test-first policy documented in
[Japanese](docs/DEVELOPMENT.ja.md).
