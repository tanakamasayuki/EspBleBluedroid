# EspBleBluedroid

EspBleBluedroid is an Arduino library for ESP32 boards using the Bluedroid
stack. It aims to provide an experience similar to its NimBLE-based sibling
library, [EspBle](https://github.com/tanakamasayuki/EspBle), especially for GATT operations after a
connection is established.

Its public API covers the root lifecycle, legacy advertising, scanning, one
Central connection, asynchronous GATT discovery, UUID/handle-based
Characteristic and Descriptor operations, Notification subscription, and an
EspBle-shaped GATT Server API with opaque registration handles, Read/Write,
Notify, and Indicate. These paths are verified by automated two-board peer
tests on original ESP32 boards. Just Works
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

Peer coverage includes the initial BLE connection-parameter snapshot and
post-connect interval, latency, and supervision-timeout updates.
Random Static and controller-managed RPA advertising, local identity reporting,
and configurable BLE transmit power are also covered over the air.

See [tests/README.md](tests/README.md) for setup and usage.

The current API and Bluetooth Classic coexistence policy is documented in
[Japanese](docs/API_DESIGN_POLICY.ja.md).
Current BLE API differences from the NimBLE-based EspBle are documented in
[Japanese](docs/BLE_BACKEND_DIFFERENCES.ja.md).
Japanese introductory guides cover
[BLE](docs/GUIDE_BLE_BASICS.ja.md) and
[Bluetooth Classic](docs/GUIDE_CLASSIC_BASICS.ja.md) separately.

The implemented and planned feature set is tracked in
[Japanese](docs/STATUS.ja.md).

The 91 examples are indexed in [examples/README.md](examples/README.md). The BLE
ones are ported from the sibling library
[EspBle](https://github.com/tanakamasayuki/EspBle) and use the same API;
[examples/DIFFERENCES_FROM_ESPBLE.md](examples/DIFFERENCES_FROM_ESPBLE.md) lists
every difference that affects how a sketch is written, and each example whose
usage differs repeats the reason in its own README.

Implementation follows the test-first policy documented in
[Japanese](docs/DEVELOPMENT.ja.md).
