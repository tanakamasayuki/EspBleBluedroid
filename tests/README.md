# Tests

> 日本語版: [README.ja.md](README.ja.md)
> What is covered, what is missing, and in which order it gets added:
> [TEST_PLAN.md](TEST_PLAN.md)

EspBleBluedroid uses `pytest-embedded` with its Arduino CLI backend for
hardware tests.

```text
unit/     backend-independent codecs, parsers, state transitions (no hardware)
peer/     two original ESP32 boards on the bundled Bluedroid stack
interop/  original ESP32 + ESP32-S3 running a released EspBle package (planned)
```

The layers, the fixtures, the coverage tables, and the priority order live in
[TEST_PLAN.md](TEST_PLAN.md). This file is the operating manual.

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
`peer/advertise_scan` verifies the public lifecycle, independent advertising
and scan-response payloads, active-scan merging with Service Data, Appearance,
and Tx Power, payload limits, value-type results, duration and explicit stopping, end-time queue
flushing, reinitialization, deferred callback dispatch through `update()`, and
deterministic 16-result queue overflow/drop accounting through a test-only seam.
`peer/advertise_payload` verifies raw AD structures, grouped UUIDs, the 31-byte
boundary, and timed advertising stop behavior.
`peer/connect_disconnect` verifies non-blocking connection requests, reconnect
IDs, default and negotiated MTU events, HCI disconnection reasons, exact timeout
classification against a non-advertising known peer,
deferred callbacks, disconnection, bounded `end()` during an in-flight attempt
and an established link, peer disconnection, and reinitialization without
stale events.
`peer/connection_parameters` verifies initial interval/latency/timeout
snapshots, a Central update request, matching negotiated values on both peers,
and callback delivery from `update()`.
`peer/local_identity` verifies Random Static and RPA advertising plus low/high
transmit-power values observed over the air.
`peer/accept_list` verifies pre-initialization rejection, idempotent entries,
controller-side rejection of an unlisted central, connection after switching
the advertising policy to `Any`, and clean disconnection.
`peer/directed_advertising` verifies a payload-free High Duty packet addressed
to the central, connection and clean disconnection, the 1.28-second High Duty
limit, and persistent Low Duty advertising with explicit stop.
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
eight-entry write-queue overflow behavior with deferred completion results for
all eight accepted writes.
`peer/spp_client` verifies asynchronous SDP/RFCOMM connection to a raw ESP-IDF
server, the shared session API, binary data, public disconnection, reconnect
IDs, deferred write completion, and deferred failure/timeout delivery.
`peer/spp_multi_backend` is a raw Bluedroid feasibility test. It opens two
simultaneous SPP sessions over one ACL by using two distinct RFCOMM server
channels, verifies handle-separated bidirectional data, and closes both
sessions. A second session to the same channel did not establish in this
two-board topology; testing multiple clients against one Server service needs
another peer.
`peer/dual_mode_scan_spp` verifies active BLE Scan and a BLE Central/GATT
connection while an SPP session remains connected. It covers service discovery,
Characteristic Read/Write, subscription/notification, and consecutive bounded
64-, 128-, and 256-notification rounds bridged to SPP without reconnecting or
resubscribing. Each round accounts for BLE event-queue drops explicitly and
verifies that every delivered notification completes its SPP round trip and
reaches the receive ring without SPP write or receive loss.
It also fills the BLE connection-event queue deterministically and verifies
that a GATT completion evicts one notification instead of being dropped.
`peer/spp_receive_buffer` verifies the session-scoped 2048-byte receive ring,
binary-safe `peek()`/single-byte/bulk reads, deterministic overflow accounting,
and buffer invalidation on disconnect against a 2300-byte raw ESP-IDF burst.
`peer/spp_serial` verifies the root-bound `EspBluedroidSppSerial` in the Server
role, including automatic tracking across two consecutive incoming sessions,
`Print` text/number output, CRLF, binary writes, automatic 990-byte write
chunking, `readBytes()`, `availableForWrite()`, `flush()`, and disconnected
behavior. `peer/spp_client` also verifies automatic tracking across two outgoing
Client sessions.
`peer/spp_security` verifies DisplayYesNo SSP against a raw ESP-IDF client:
matching six-digit values, explicit rejection, authentication failure,
rediscovery and retry, authenticated/encrypted session state, deferred Classic
Security callbacks, Classic bond listing/deletion, secure reconnection from a
stored link key without another confirmation, and binary data in both public
SPP Server and Client roles.
`peer/spp_passkey` verifies Classic DisplayOnly/KeyboardOnly Passkey Entry in
both public-library roles, deferred address-scoped display/request callbacks,
runtime passkey submission, unanswered-input timeout, rejection of late input,
successful retry, bounded shutdown while input is pending,
authenticated/encrypted SPP data, and reinitialization with the opposite I/O
capability in the same boot.
`peer/a2dp_sink` verifies the public A2DP Sink and AVRCP Controller against a
raw Source/Target, including PCM, Play press/release, absolute volume, callback
contexts, disconnection, and shutdown.
`peer/a2dp_source` verifies the public A2DP Source and AVRCP Target against a
raw Sink/Controller, including PCM, Pause press/release, absolute volume,
callback contexts, disconnection, and shutdown.
`peer/hfp_backend` verifies public HFP Hands-Free and Audio Gateway roles
end-to-end, including SLC, SCO, bidirectional mono PCM through the built-in
CVSD/mSBC codec, and disconnection.
`peer/gatt_server` also verifies a real Indication: the peer rewrites the CCCD
with 0x0002 and the send result reports success only after the confirmation.
`peer/duplicate_uuid` verifies that the server rejects a duplicate Characteristic
UUID inside one Service, a duplicate Descriptor UUID under one Characteristic, and
a malformed UUID string, each with the exact error and detail, that the same UUID in another Service is
accepted, and that the client reaches both of a peer's two same-UUID
characteristics by attribute handle, with notifications routed to the sending
handle.
`peer/gatt_disconnect_purge` verifies that `disconnect()` during an in-flight read
is accepted, that the read produces exactly one completion with no dropped
events, and that the next connection discovers and reads normally.
`peer/long_value` verifies that a value longer than the MTU is returned whole by
both the UUID and the handle read forms, byte-for-byte against the peer's ramp.
`peer/service_changed` verifies that Generic Attribute 0x1801 and Service Changed
0x2a05 come from the stack, which the application neither registers nor triggers.

## EspBle interoperability suite (`interop/`, planned)

Cross-stack tests against EspBle (NimBLE) live in this `tests/` tree. They
consume a released EspBle package pinned by version and artifact checksum, never
`../EspBle` or a development branch.

Because EspBle does not run on the original ESP32, the second board is an
ESP32-S3 (profile `s3_peer_device`). Configure its port to enable the suite; it
skips itself when the port is absent, so a bare `pytest` still completes with
the two permanently connected ESP32 boards.

```dotenv
TEST_SERIAL_PORT_PEER_DEVICE_S3_PEER_DEVICE=/dev/ttyUSB2
```

```sh
uv run --env-file .env pytest interop/
```

Only scenarios that pytest can build, flash, drive, assert, time out, and clean
up without human interaction are in scope. Smartphone UI, manual pairing, and
subjective audio checks remain outside this suite. Package updates are explicit
reviewed changes rather than automatic latest-release tracking.

The scenario list, the package pinning rules, and the skip semantics are in
[TEST_PLAN.md](TEST_PLAN.md#espble-release-package-interop-suite-interop).
