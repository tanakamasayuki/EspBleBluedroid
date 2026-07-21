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
