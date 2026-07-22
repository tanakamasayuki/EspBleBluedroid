# EspBleBluedroid

EspBleBluedroid is an Arduino library for ESP32 boards using the Bluedroid
stack. It aims to provide an experience similar to its NimBLE-based sibling
library, [EspBle](../EspBle/), especially for GATT operations after a
connection is established.

The project is in its initial bring-up stage. Its public API currently covers
the root lifecycle, legacy advertising, scanning, one Central connection, and
asynchronous GATT discovery, UUID/handle-based Characteristic operations,
Descriptor Read/Write, and Notification subscription,
verified by automated two-board peer tests on original ESP32 boards. Just Works
security and BLE bond management are also available. MITM pairing and
Bluetooth Classic APIs will continue to be added test-first.

See [tests/README.md](tests/README.md) for setup and usage.

The current API and Bluetooth Classic coexistence policy is documented in
[Japanese](docs/API_DESIGN_POLICY.ja.md).

The implemented and planned feature set is tracked in
[Japanese](docs/STATUS.ja.md), with examples in [examples](examples/README.md).

Implementation follows the test-first policy documented in
[Japanese](docs/DEVELOPMENT.ja.md).
