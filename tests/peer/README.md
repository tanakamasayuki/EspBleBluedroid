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
`duplicate_uuid_server` verifies that two Characteristics sharing one UUID inside
one Service, published by this library, are two real attributes on the air: a raw
Arduino-ESP32 central walks the handle-keyed map (the wrapper's UUID-keyed map can
only return one of a duplicated pair) and reads both values, both
Report-Reference-style descriptors, and both CCCDs, while a write aimed at the
second handle is attributed to the second characteristic and each notification
arrives on the handle that sent it. This is the prerequisite HID over GATT rests
on, since a keyboard's Report characteristics all carry UUID 0x2a4d.
`hid_keyboard_device` verifies the HID over GATT keyboard against a raw
Arduino-ESP32 central standing in for a host OS: the Report Map read (long, at the
default MTU) carries exactly the descriptor `tests/unit/hid_report_maps` pins, the
two 0x2A4D Report characteristics are told apart by their Report Reference
descriptors, an input report notification carries the 8-byte keyboard layout, a
host's LED write comes back through `onOutputReport()` and `ledState()`, a
Protocol Mode write is reported, and the Device Information and Battery values are
the ones `configure()` was given. It also pins the two refusals a caller has to be
able to tell apart — `no connected HID Host` and `no subscribed HID Host` — and
that a disconnected host leaves neither `ready()` nor an LED state behind.
`hid_composite` verifies the five device profiles that share one HID service —
keyboard, mouse, consumer control, system control and gamepad. What only a
composite device can get wrong is checked: the published Report Map is the five
descriptors concatenated in profile order with the mouse button count patched in
(the expected value is built from the same snapshot `unit/hid_report_maps` uses, so
the composition rule itself is the assertion), five Input Report characteristics
share UUID 0x2A4D with a Report Reference each, and every notification arrives on
the handle belonging to the profile that sent it, with the exact wire bytes. The
attribute order (configuration order) and the descriptor order inside the Report
Map (profile order) deliberately differ, because a host uses neither — it uses the
Report Reference.

`hid_vendor_custom` verifies the two profiles whose payload the library does not
interpret: `hidVendor()`, whose descriptor is fixed but whose report size is the
caller's, and `hidCustom()`, whose descriptor is the caller's entirely. They are
the only profiles a host writes to, so this suite covers the direction
`hid_composite` cannot: the Report Map is the composed built-in descriptor followed
by the sketch's own (built here from the same snapshot, with the vendor size patched
at a non-default value so the patch is visible on the air); a report ID an enabled
built-in profile owns is refused to `hidCustom()`; six Report characteristics share
UUID 0x2A4D and their write properties match the type each Report Reference declares
— an Input report notifies, an Output report also takes Write Without Response, and
a Feature report is configuration, so it is always acknowledged; and the Output and
Feature reports the host writes reach the callbacks byte for byte, from the caller's
`update()`. The reports are 40 bytes, which does not fit an ATT payload at the
default MTU, so the device's own view of the negotiated MTU is asserted before the
bytes are — a truncation and a wrong report would otherwise look alike. The refusals
are pinned too: a length other than the declared one is `InvalidArgument`, and an
undeclared report ID is `NotFound` rather than a silently invented characteristic.
