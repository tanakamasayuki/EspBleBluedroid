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
`peer/spp_server` verifies a public SPP Server against a raw ESP-IDF client,
including binary-safe bidirectional data, new IDs on reconnect, remote
disconnection, bounded shutdown while the server is running, and ordered
eight-entry write-queue overflow behavior.
`peer/spp_client` verifies asynchronous SDP/RFCOMM connection to a raw ESP-IDF
server, the shared session API, binary data, public disconnection, reconnect
IDs, and deferred failure/timeout delivery.
`peer/dual_mode_scan_spp` verifies active BLE Scan while an SPP session remains
connected, then starts binary SPP traffic from the deferred Scan callback.
`peer/spp_receive_buffer` verifies the session-scoped 2048-byte receive ring,
binary-safe `peek()`/single-byte/bulk reads, deterministic overflow accounting,
and buffer invalidation on disconnect against a 2300-byte raw ESP-IDF burst.
`peer/spp_stream` verifies that an established session can be attached to an
Arduino `Stream`, including `Print` text/number output, CRLF, binary writes,
automatic 990-byte write chunking, `readBytes()`, `availableForWrite()`,
`flush()`, invalid attachment, and automatic disconnected behavior.
`peer/spp_security` verifies DisplayYesNo SSP against a raw ESP-IDF client:
matching six-digit values, explicit rejection, authentication failure,
rediscovery and retry, authenticated/encrypted session state, deferred Classic
Security callbacks, Classic bond listing/deletion, secure reconnection from a
stored link key without another confirmation, and binary data in both public
SPP Server and Client roles.
