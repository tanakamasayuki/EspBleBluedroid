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
flushing, reinitialization, deferred callback dispatch through `update()`, and
deterministic 16-result queue overflow/drop accounting through a test-only seam.
`peer/advertise_payload` verifies raw AD structures, grouped UUIDs, the 31-byte
boundary, and timed advertising stop behavior.
`peer/connect_disconnect` verifies non-blocking connection requests, reconnect
IDs, exact timeout classification against a non-advertising known peer,
deferred callbacks, disconnection, bounded `end()` during an in-flight attempt
and an established link, peer disconnection, and reinitialization without
stale events.
`peer/gatt_client` verifies public asynchronous Characteristic and Descriptor Read, both Write
modes, Notification subscription/unsubscription, binary-safe values,
connection-scoped database snapshots, peer reception, disconnect invalidation,
handle-based Characteristic targeting, and callback dispatch from `update()`.
`peer/security_bond` verifies Just Works pairing, encrypted GATT access, bond
storage, encrypted reconnection, deferred security callbacks, and bond cleanup.
`peer/security_passkey` verifies static-passkey MITM, deferred passkey display,
authenticated connection state, authenticated GATT access, and bond storage.
`peer/runtime_passkey` verifies KeyboardOnly runtime passkey entry against a
DisplayOnly peer, including the generated passkey relay, authenticated and
bonded state, bounded disconnect and `end()` while input is pending,
reinitialization, deterministic unanswered-input timeout, successful pairing
afterward, and bond cleanup.
`peer/numeric_comparison` verifies DisplayYesNo Numeric Comparison, matching
six-digit values on both boards, explicit rejection without implicit link
disconnection, explicit disconnect and successful retry, confirmation through
`update()`, deterministic unanswered-confirmation timeout, authenticated and
bonded state, and bond cleanup.
`peer/classic_inquiry` verifies dual-mode initialization, the compile-time
capability snapshot, Classic name/Class of Device/RSSI results, cancellation
from a result callback, and completion delivery from `update()`.
