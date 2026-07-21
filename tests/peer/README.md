# Peer Tests

These tests connect two original ESP32 boards over BLE without signal wiring.
The parent sketch is currently the central and `peer_device/` is the
peripheral. `stack_smoke` directly uses the Arduino-ESP32 bundled Bluedroid API
so the hardware and test harness can be validated before the public library API
is implemented.

