# Tests

EspBleBluedroid uses `pytest-embedded` with its Arduino CLI backend for
hardware tests. The initial test connects two original ESP32 boards over BLE
using the bundled Bluedroid API.

```sh
cd tests
cp .env.example .env
uv sync
uv run --env-file .env pytest
```

The default ports are `/dev/ttyUSB0` for the central and `/dev/ttyUSB1` for the
peripheral. `.env` is ignored by Git; edit only that local file when ports vary
between machines or USB connection order. Running the test flashes both boards
and overwrites their existing firmware.

`peer/stack_smoke` verifies the underlying Bluedroid connection and GATT path.
`peer/advertise_scan` verifies the public lifecycle, advertising payload limit,
scanning, value-type results, duration and explicit stopping, end-time queue
flushing, reinitialization, and deferred callback dispatch through `update()`.
`peer/advertise_payload` verifies raw AD structures, grouped UUIDs, the 31-byte
boundary, and timed advertising stop behavior.
`peer/connect_disconnect` verifies non-blocking connection requests, reconnect
IDs, asynchronous failures, deferred callbacks, disconnection, bounded
in-flight cancellation through `end()`, and reinitialization without stale events.
`peer/gatt_client` verifies public asynchronous Characteristic Read, binary-safe
values, connection-ID validation, and callback dispatch from `update()`.
