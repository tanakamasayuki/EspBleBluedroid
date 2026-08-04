# Peer Tests

These tests connect two original ESP32 boards over BLE without signal wiring.
The parent sketch is currently the central and `peer_device/` is the
peripheral. `stack_smoke` directly uses the Arduino-ESP32 bundled Bluedroid API
so the hardware and test harness can be validated independently of the public
library API. `advertise_scan` verifies the public lifecycle, advertising and its
payload limit, scanning, and deferred result delivery through `update()`.
`advertise_payload` parses the raw PDU to verify grouped service UUIDs, the
31-byte boundary, and timed stop behavior.
`connect_disconnect` verifies the public Central connection lifecycle, reconnect
IDs, unreachable-peer failures, deferred callbacks, disconnection, and stack
reinitialization.
`classic_inquiry` verifies the public Classic capability snapshot and Inquiry
against a discoverable Classic-only peer, including deferred cancellation
completion.
`spp_server` verifies the public SPP Server against a raw ESP-IDF client,
including binary data, ordered write-queue overflow, reconnect IDs, remote
disconnect, and stack shutdown.
`spp_client` verifies public asynchronous Client connection to a raw ESP-IDF
server, shared sessions, local disconnect, reconnect, and failure delivery.
`spp_multi_backend` is a raw Bluedroid feasibility test for two simultaneous
SPP sessions over one ACL using distinct RFCOMM server channels. It verifies
handle-separated bidirectional data and cleanup without claiming public
multi-session support.
`dual_mode_scan_spp` verifies BLE Scan and binary SPP traffic on one active
dual-mode stack.
`multi_listener` verifies multi-observer dispatch: the primary `on*()` callback
plus `add*Listener()` on the connection, GATT client and GATT Server events, in
registration order, with the four-listener limit, per-owner ids, and the rule that
a listener added during a dispatch is left out of it.
`peripheral_connection` verifies the peripheral half of the connection lifecycle
against a raw Arduino-ESP32 BLE client: the connect event, the MTU exchange this
side only observes, the `connection()` snapshot with its parameters, pairing in
that role, the HCI disconnection reason, and the connection ID a GATT Server event
carries.
`midi_device` and `midi_host` verify the BLE MIDI profile helpers
(`EspBleMidiProfile.h`) in both roles against a raw Arduino-ESP32 peer that
encodes and decodes the BLE MIDI header with its own arithmetic, so the wire
format is checked against the specification rather than against the same codec on
both ends: the timestamp header, running status carried across a packet, an
interleaved System Real-Time byte, and a SysEx spread over several packets in each
direction — plus the refusal of a second transfer while one is in flight. The BLE
MIDI UUIDs are fixed by the specification, so these two suites are isolated by
device name instead of by a suite UUID tag.
