# Tests

> 日本語版: [README.ja.md](README.ja.md)
> What is covered, what is missing, and in which order it gets added:
> [TEST_PLAN.md](TEST_PLAN.md)

EspBleBluedroid uses `pytest-embedded` with its Arduino CLI backend for
hardware tests.

```text
unit/     backend-independent codecs, parsers, state transitions (no hardware)
peer/     two original ESP32 boards on the bundled Bluedroid stack
interop/  original ESP32 + ESP32-S3 running a released EspBle package
```

The layers, the fixtures, the coverage tables, and the priority order live in
[TEST_PLAN.md](TEST_PLAN.md). This file is the operating manual.

```sh
cd tests
cp .env.example .env
uv sync
uv run --env-file .env pytest
```

The default ports are `/dev/ttyUSB0` for the central, `/dev/ttyUSB1` for the
peripheral, and `/dev/ttyACM0` for the ESP32-S3 that `interop/` uses. `.env` is
ignored by Git; edit only that local file when ports vary
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
`peer/multi_listener` covers multi-observer dispatch: the primary `on*()` callback
and the `add*Listener()` observers of the connection, GATT client and GATT Server
events, the order they run in, the four-listener limit, per-owner listener ids, and
the rule that a listener added during a dispatch is left out of it.

`peer/midi_device` and `peer/midi_host` cover the BLE MIDI profile helpers in
both roles against a raw Arduino-ESP32 peer that encodes and decodes the BLE MIDI
header itself: the timestamp header, running status carried across a packet, an
interleaved System Real-Time byte, a SysEx spread over several packets in each
direction, and the refusal of a second transfer while one is in flight. The host
side also pins the chaining a serialized central forces: consecutive sends go out
from the previous write's completion.

`peer/peripheral_connection` covers the peripheral half of the connection
lifecycle: a peer connecting to this device's GATT Server produces the connect
event, the MTU event, a `connection()` snapshot with the negotiated parameters, a
pairing event, and the HCI disconnection reason, all with
`localRole = Peripheral`, and the connection ID a server event carries resolves
through `connection()`. The peer is a raw Arduino-ESP32 BLE client.

`peer/gatt_server` also verifies a real Indication: the peer rewrites the CCCD
with 0x0002 and the send result reports success only after the confirmation.
`peer/duplicate_uuid` verifies that the server accepts a duplicate Characteristic
UUID inside one Service with a handle of its own, that it still rejects a
duplicate Descriptor UUID under one Characteristic and a malformed UUID string
with the exact error and detail, that the same UUID in another Service is
accepted, and that the client reaches both of a peer's two same-UUID
characteristics by attribute handle, with notifications routed to the sending
handle.
`peer/hid_keyboard_device` verifies the HID over GATT keyboard against a raw
central standing in for a host OS: the descriptor that reaches the air, the two
0x2A4D Report characteristics told apart by their Report Reference descriptors, the
input report notification, the LED output write, the Protocol Mode write, and the
Device Information and Battery values.
`peer/hid_composite` verifies the five profiles that share one HID service, the
Report Map they compose, and that each notification arrives on its own report's
handle.
`peer/hid_boot_protocol` verifies HID over GATT Boot Protocol on an NKRO keyboard:
the two extra characteristics Boot Protocol adds, the Protocol Mode default and
switch, the same usages leaving as a 29-byte bitmap or as the fixed 8-byte boot
report depending on the mode, the HID rollover code when more than six keys are
held, the boot LED write, and `ready()` following the CCCD of whichever report the
selected mode uses.
`peer/hid_vendor_custom` verifies the two profiles whose payload the library does
not interpret — `hidVendor()` and `hidCustom()` — and the direction only they have:
a Report Map that is the composed descriptor followed by the sketch's own, a report
ID a built-in profile owns being refused, six 0x2A4D reports whose write properties
match the type each Report Reference declares, and Output and Feature reports the
host writes arriving byte for byte.
`peer/duplicate_uuid_server` verifies the other side of that: two same-UUID
characteristics published by this library are two real attributes on the air, read
back from a raw peer with their own values, their own Report-Reference-style
descriptors, per-handle write attribution, one CCCD each, and notifications
arriving on the handle that sent them. Registration alone cannot show this — a
backend that reused the first entry would pass every local check.
`peer/gatt_disconnect_purge` verifies that `disconnect()` during an in-flight read
is accepted, that the read produces exactly one completion with no dropped
events, and that the next connection discovers and reads normally.
`peer/long_value` verifies that a value longer than the MTU is returned whole by
both the UUID and the handle read forms, byte-for-byte against the peer's ramp.
`peer/service_changed` verifies that Generic Attribute 0x1801 and Service Changed
0x2a05 come from the stack, which the application neither registers nor triggers.

## EspBle interoperability suite (`interop/`)

Cross-stack tests against EspBle (NimBLE). The EspBle side runs on the
permanently connected ESP32-S3 — the parent fixture, profile `s3_peer_host` — with
the version pinned in each `sketch.yaml` (`EspBle (1.1.0)`); the second board is
the same original ESP32 the `peer/` suites use.

```dotenv
TEST_SERIAL_PORT_S3_PEER_HOST=/dev/ttyACM0
```

```sh
uv run --env-file .env pytest interop/
```

All three boards are permanently connected, so a bare `pytest` covers this suite
too. Procedure and rules: [interop/README.md](interop/README.md); scenario list:
[TEST_PLAN.md](TEST_PLAN.md#espble-release-package-interop-suite-interop).

`interop/gatt_basic` verifies a Bluedroid central against an EspBle peripheral:
the MTU 247 exchange, discovery including the declared properties, characteristic
read, write with and without response, descriptor read/write, notification,
indication with its confirmation, unsubscribe, and disconnection.

`interop/advertise_scan` verifies advertising and scanning in both directions: a
payload built by one stack's builder — name, manufacturer data, Service Data,
Appearance, Tx Power, split across the advertising payload and the scan response —
reconstructed field for field by the other stack's parser, plus a passive scan of
the same advertiser that must see the advertising payload alone.

`interop/long_value` reads a 300-byte value published by an EspBle peripheral
across the 247-byte MTU, through both the UUID form and the handle form, checking
every byte against the peer's ramp.

`interop/duplicate_uuid` works with two characteristics that share a UUID inside
one service on the EspBle peripheral — the shape this library's server API
rejects — attributing discovery, reads, the write, the subscription, and the
notification to a handle on both sides.

`interop/security` pairs across the two stacks four times: Just Works, static
passkey (DisplayOnly against KeyboardOnly, which is what selects Passkey Entry),
and Numeric Comparison both confirmed and refused. Encrypted, authenticated,
bonded and the key size are asserted on both sides, the attribute tiers show which
link may reach what, the two implementations must derive the same six digits, and a
refusal must leave no encryption and no bond.

`interop/profile_wire` exchanges values built with the shared codec headers — a
FLOAT32, a CGM measurement with its E2E-CRC, an SFLOAT the other way, and an
iBeacon — each asserted as the wire bytes and as the decode. Here the library under
test is the GATT server and the beacon, and EspBle is the central.

`interop/midi` runs BLE MIDI across the two stacks in both directions, each side
encoding and decoding with its own library: channel-voice messages of two, one and
no data bytes, and a 99-byte SysEx reassembled with its ramp intact. The codec and
the profile helper are already checked without hardware, so what this adds is the
transport — the CCCD write, notifications against the negotiated MTU, Write Without
Response, and a SysEx spanning packets — and the second test swaps the roles,
because notifying as a peripheral and writing as a central are different paths.
