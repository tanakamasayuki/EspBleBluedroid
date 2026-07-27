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
