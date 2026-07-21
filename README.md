# EspBleBluedroid

EspBleBluedroid is an Arduino library for ESP32 boards using the Bluedroid
stack. It aims to provide an experience similar to its NimBLE-based sibling
library, [EspBle](../EspBle/), especially for GATT operations after a
connection is established.

The project is currently at its initial bring-up stage. Its first milestone is
an automated two-board peer test using two original ESP32 boards. The public
library API has not been implemented yet.

See [tests/README.md](tests/README.md) for setup and usage.

The current API and Bluetooth Classic coexistence policy is documented in
[Japanese](docs/API_DESIGN_POLICY.ja.md).

Implementation follows the test-first policy documented in
[Japanese](docs/DEVELOPMENT.ja.md).
