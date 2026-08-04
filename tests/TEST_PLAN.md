# Test plan

> 日本語版: [TEST_PLAN.ja.md](TEST_PLAN.ja.md)
> How to run: [README.md](README.md)
> Design context: [docs/API_DESIGN_POLICY.ja.md](../docs/API_DESIGN_POLICY.ja.md) (Japanese),
> current state: [docs/STATUS.ja.md](../docs/STATUS.ja.md) (Japanese)

This plan reuses the structure of the sibling library
[EspBle](https://github.com/tanakamasayuki/EspBle)'s `tests/TEST_PLAN.md` and adds
the layers that only exist here: Bluetooth Classic, dual mode, and Bluedroid
backend constraints. A scenario that shares its name with an EspBle scenario
means the same thing; only the differences are spelled out.

## Approach

For both BLE and Classic, connection, disconnection, discovery, subscription,
security, and bonding span several asynchronous events. Peer tests are therefore
the primary automated tests that drive the implementation, not an auxiliary
smoke check.

- **unit**: backend-independent codecs, parsers, and state transitions verified
  with host g++ (`tests/unit/`). No hardware. The machine-checked API parity
  described below also lives here.
- **examples_compile**: build regression for the public API on the target SoC.
  `arduino-cli compile --profile esp32` over every example (the ✅ in the build
  column of the coverage tables means this check).
- **peer**: two original ESP32 boards as the standard fixture, exercising the
  real radio, controller, and host stack (`tests/peer/`).
- **interop**: one original ESP32 (this library) plus one ESP32-S3 running a
  **published EspBle release package**, to verify Bluedroid ↔ NimBLE
  cross-stack behaviour (`tests/interop/`, added as features land).
- **manual**: interoperability with phones, PCs, and commercial devices. Never
  mixed into automated pass criteria; recorded in the
  [release checklist](../docs/RELEASE_CHECKLIST.md).

There is no "single" layer for peer-free runtime behaviour on one board. It will
be added if a scenario needs it.

## Peer hardware

| Fixture | Host DUT | Second peer | Purpose | Connection |
|---|---|---|---|---|
| Standard regression | original ESP32 | original ESP32 | Full public API, Bluedroid paths, Classic, dual mode | always connected |
| EspBle interop | ESP32-S3 (EspBle release) | original ESP32 | Bluedroid ↔ NimBLE wire and procedure agreement | always connected (EspBle's main board) |
| Manual interop | original ESP32 | phone / PC / commercial device | Interoperability with OS stacks | manual |

EspBle does not run on the original ESP32 — it drives NimBLE directly and
excludes classic ESP32 because of Core constraints. The second board of the
interop fixture is therefore always a NimBLE-capable SoC such as an S3. Neither
BLE nor Classic needs wiring between boards; only serial/power to the PC.

The existing pytest-embedded-cli conventions apply.

- Host profile: `esp32_peer_host` / `s3_peer_host` (the EspBle side of interop)
- Second-board profile: `esp32_peer_device`
- Second-board directory: `peer_device/`
- Python fixture: `peers["device"]`

Interop makes the S3 the parent fixture and reuses the same second ESP32 as
`peer/`, so the only extra setting is `TEST_SERIAL_PORT_S3_PEER_HOST`. That board
is EspBle's own permanently connected fixture, so a bare `pytest` includes the
interop suite.

`host` / `device` are pytest fixture names, not BLE or Classic roles. Current
scenarios pin the host side to central / SPP client / one audio role and do not
assume the roles can be swapped. When the public API is verified as a central,
the host output carries the assertions; when it is verified as a peripheral, the
peer output does.

## Pinning EspBle API agreement with tests

"Match EspBle except where the backend forces a difference"
([docs/API_DESIGN_POLICY.ja.md](../docs/API_DESIGN_POLICY.ja.md)) decays if it
lives only in prose. Four tests hold it in place.

1. **Names and shapes (unit).** `api_parity` diffs the public symbols of
   `EspBle.h` and `EspBleBluedroid.h` — classes, methods, struct fields, enum
   constants — against the allowlist in `docs/API_PARITY.tsv`. Any difference
   not listed there with a reason fails the test. EspBle-only APIs, this-library
   -only APIs, and same-name-different-signature APIs must all be classified.
   - The reason is one of `backend` (a Bluedroid constraint), `classic` (a
     Classic extension EspBle does not have), or `planned` (not implemented yet;
     a link to the plan or issue is required). Anything still marked `planned`
     is not called "EspBle compatible".
2. **Shared wire expectations (peer / interop).** A scenario that shares its
   name with an EspBle scenario uses the same expected hex bytes. A mismatch is
   treated as an implementation bug, not a backend difference.
3. **Every difference surfaces as an explicit error (peer).** A request the
   backend cannot honour never silently succeeds or silently does nothing: it
   sets `lastError()` with a reason string, and the test pins that string
   (duplicate characteristic UUID, legacy payload overflow, a second concurrent
   GATT operation, …).

4. **Returned values, not only names and shapes (unit).** Agreement on every
   signature still leaves what a function *returns*, which no header shows.
   `api_parity` therefore also compares the enum-to-string maps of the `*Name()`
   functions against the `espble.values` snapshot, listing each difference in the
   same table. This was found the hard way: `lastErrorName()` returned
   `INVALID_ARGUMENT` in EspBle and `InvalidArgument` here, so a sketch that
   logged or compared the string did not port. This library now uses EspBle's
   spelling, and the only remaining entry is `UNSUPPORTED`, whose enum constant
   exists only here. The functions are found by shape, so a second name map added
   later is compared without touching the test.

The Classic extensions are held to the same standard. `classic().spp()`,
`classic().a2dpSink()`, and the other session APIs are verified with the same
vocabulary as the EspBle connection API: asynchronous request → completion event
dispatched from `update()`, runtime IDs, `lastError()`, bounded queues with drop
accounting. **"The Classic side does not invent its own conventions" is itself a
test objective.**

## Test UUID allocation

Another test suite may be running nearby at the same time, so the design assumes
it. To keep an EspBle peer test running next to this one from ever being
connected to by mistake, this repository uses **its own UUID space**.

```text
SSSSNNNN-b1dd-4d00-9e5a-627564726f69
^^^^     suite tag (16-bit, table below)
    ^^^^ attribute number inside that suite (0000 = service, 0001+ = characteristics / descriptors)
```

The `627564726f69` tail is ASCII `budroi` and matches no UUID on the EspBle side.
Add a row here before using a new tag.

| Suite tag | Suite |
|---|---|
| `0001` | `gatt_disconnect_purge` |
| `0002` | `service_changed` marker service |
| `0003` | `duplicate_uuid` |
| `0004` | `long_value` |
| `0005` | `security_bond` |
| `0006` | `security_passkey` |
| `01xx` | reserved for interop scenarios (`0100` = `interop/gatt_basic`, `0101` = `interop/advertise_scan`, `0102` = `interop/long_value`, `0103` = `interop/duplicate_uuid`, `0104` = `interop/security`, `0105` = `interop/profile_wire`) |

Suites not in the table still use individually chosen 128-bit UUIDs from before
this scheme (`8d47a6xx`, `6b976bxx`, `48e8c1xx`, …). Those are confirmed not to
collide with EspBle either, and move to the scheme whenever they are touched.
`security_bond` and `security_passkey` **used the same UUIDs as EspBle** and were
migrated first, since they were an actual source of crosstalk.

Absence of overlap is checkable:

```sh
comm -12 \
  <(grep -rhoiE "[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}" tests/peer --include=*.ino | tr A-F a-f | sort -u) \
  <(grep -rhoiE "[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}" ../EspBle/tests/peer --include=*.ino | tr A-F a-f | sort -u)
```

Standard profile UUIDs (0x180a, 0x2a05, …) are fixed by specification and
therefore shared. A profile suite **advertises its own marker service UUID** to
choose its peer, and never picks a peer by a standard UUID alone.

## Peer test principles

- Use a test-only 128-bit service UUID and a dedicated RFCOMM name to exclude
  nearby devices, taking an unused suite tag from the allocation table above.
- Never pick the peer by device name alone.
- Where practical, implement one side directly on the bundled Arduino-ESP32 API
  or on raw ESP-IDF/Bluedroid. If both sides run the public API, a wrong
  assumption cancels itself out and stays invisible.
- Keep every scenario assertable from serial logs alone.
- Stop scan, advertising, subscriptions, connections, SPP sessions, and audio
  streams at the end of each test.
- Security tests state the bond/NVS state at start and end. BLE bonds and
  Classic bonds are separate stores, so the test name and output say which one
  was inspected.
- Timeouts may absorb transient radio delays, but unbounded retries must never
  hide a defect.
- Cross-check disconnect reasons, MTU, and security state on both sides where
  possible.
- **Assert `update()` dispatch explicitly.** Callbacks must run in the
  application `loop()` context, not on the stack task; the output carries
  `context=loop`. The A2DP/HFP PCM callbacks are the deliberate exception (stack
  task), and their context is printed the same way.
- **Overflow bounded queues and count them.** The 16-entry scan queue, the BLE
  connection event queue, the 8-entry SPP write queue, and the 2048-byte SPP RX
  ring are not specified by their limits; what is pinned is that the drop
  accounting (`droppedResultCount()`, `droppedEventCount()`, …) is correct when
  they overflow.
- **Test-only seams** (`ESP_BLE_BLUEDROID_TESTING`) are for paths that cannot be
  reproduced deterministically from outside — queue overflow, shortened security
  timeouts — never for paths the public API can reach.

## EspBle release-package interop suite (`interop/`)

Bluedroid talking to Bluedroid cannot reveal an implementation that depends on
Bluedroid's quirks. The cross-stack tests against EspBle (NimBLE) live in this
repository.

### Pinning the dependency

- The in-development `../EspBle`, its default branch, and unreleased commits are
  **never the reference**.
- Pin a published release from the Arduino library index in `sketch.yaml`
  (`libraries: [- EspBle (1.1.0)]`), the same way the platform version is pinned.
  Arduino CLI installs exactly that release; nothing is fetched or unpacked by
  hand. How to run it: [interop/README.md](interop/README.md).
- The installed package is never patched. If a scenario only passes with a change
  on the EspBle side, that fact is recorded in the result instead.
- Version bumps come from the separate release tooling as explicit changes,
  reviewed together with the diff and a full interop run — never automatic
  tracking of the latest release.

### Runs

All three boards are permanently connected, so a bare `pytest` covers every layer.
To run this one alone:

```sh
uv run --env-file .env pytest interop/
```

`.env` carries `TEST_SERIAL_PORT_S3_PEER_HOST` next to the two original ESP32
ports. The suite needs no conftest hook of its own: port settings and
`sketch.yaml` are the whole configuration.

### Scenarios (added as each layer settles)

| Scenario | Content |
|---|---|
| `interop/gatt_basic` | ✅ Bluedroid central ↔ EspBle peripheral: MTU 247 exchange, discovery including declared properties, read, write with and without response, descriptor read/write, notify, indicate with its confirmation, unsubscribe, disconnect. The reverse direction waits for the peripheral connection snapshot |
| `interop/advertise_scan` | ✅ Advertising / scan response built by EspBle's payload builder reconstructed field-for-field by the Bluedroid scanner's per-address merge, and the reverse. A passive scan of the same advertiser must see the advertising payload's fields and nothing from the scan response |
| `interop/security` | ✅ Just Works and static-passkey Passkey Entry across stacks, with encrypted / authenticated / bonded / key size asserted on *both* sides, the bond recorded by both, and the two attribute permission tiers exercised (an authenticated characteristic is refused on a Just Works link and reachable on a Passkey Entry one). Numeric Comparison and the Bluedroid peripheral side remain: the latter waits for the connection snapshot |
| `interop/profile_wire` | ✅ Values built with the shared headers (`EspBleMedicalFloat.h`, `EspBleCgmCrc.h`, `EspBleIBeacon.h`) decode to the same value on the other stack, asserted as both the wire bytes and the decode in milli-units: FLOAT32 by read and by notification, a CGM E2E-CRC one copy appends and the other verifies, an SFLOAT the other way round, and an iBeacon decoded from the advertisement alone. Roles reversed for the first time in interop — the library under test is the server and the beacon |
| `interop/duplicate_uuid` | ✅ Spec-legal duplicates (an EspBle peripheral with two same-UUID characteristics in one service) handled by the Bluedroid client through handle-addressed operations: discovery keeps both apart, the UUID form reaches the first, reads/write/subscribe/notification are each attributed to a handle on both sides. The server-side rejection is recorded in the same file |
| `interop/long_value` | ✅ A value longer than the negotiated MTU, published by an EspBle peripheral, arrives whole through both the UUID form and the handle form of the read. `peer/long_value` has Bluedroid on both ends, so this is what makes the claim about the client rather than about the pair |
| `interop/hid` / `interop/midi` | After HID over GATT / BLE MIDI land; device and host roles in both directions |

Only scenarios whose verdict can be decided unattended are in scope. Phone
interaction, GUI checks, listening tests, and manual pairing stay out and belong
to the manual interoperability section of the release checklist.

## Coverage plan

`build` is example compilation, `peer` is two original ESP32 boards, `interop`
is the cross-stack suite against EspBle.

### Common BLE surface (same objectives as EspBle)

| Area | unit | build | peer | interop |
|---|---|---|---|---|
| Test fixture / backend feasibility | | ✅ | ✅ `stack_smoke` | |
| Advertising / scan parser | planned | ✅ | ✅ `advertise_scan` / `advertise_payload` | ✅ `advertise_scan` |
| Scan response split / Appearance / Tx Power | | ✅ | ✅ inside `advertise_scan` | ✅ inside `advertise_scan` (active merge vs passive) |
| Advertising Service Data (AD 0x16) | | ✅ | ✅ inside `advertise_scan` | ✅ inside `advertise_scan` |
| Non-connectable broadcast | | ✅ | ✅ `ibeacon` | |
| iBeacon encode / decode | ✅ `unit/ibeacon` | ✅ | ✅ `ibeacon` | ✅ `profile_wire` |
| UUID codec | ✅ `unit/uuid` | ✅ | — | |
| Connect / disconnect / timeout / reason | | ✅ | ✅ `connect_disconnect` | ✅ `gatt_basic` |
| MTU exchange (23 → negotiated, deferred request) | | ✅ | ✅ inside `connect_disconnect` | ✅ `gatt_basic` |
| Connection parameters | | ✅ | ✅ `connection_parameters` | |
| Own address / Tx Power | | ✅ | ✅ `local_identity` | |
| Filter Accept List (advertising / scan) | | ✅ | ✅ `accept_list` | |
| Directed advertising | | ✅ | ✅ `directed_advertising` | |
| GATT client discovery / read / write / descriptor / notify | ✅ `unit/codec` | ✅ | ✅ `gatt_client` | ✅ `gatt_basic` |
| GATT client handle-addressed ops (duplicate UUIDs) | | ✅ | ✅ `duplicate_uuid` | ✅ `duplicate_uuid` |
| GATT client one-operation-at-a-time and explicit rejection | | ✅ | ✅ inside `gatt_client` | |
| Reading a value above the MTU (the whole value arrives) | | ✅ | ✅ `long_value` | ✅ `long_value` |
| GATT server read / write / descriptor / CCCD / notify | | ✅ | ✅ `gatt_server` | ✅ `gatt_basic` |
| GATT server **indicate** (issued and confirmed) | | ✅ | ✅ `gatt_server` / `service_changed` | ✅ `gatt_basic` |
| GATT server duplicate-UUID rejection error | | ✅ | ✅ `duplicate_uuid` | |
| Service Changed (0x2A05) | | ✅ | ✅ `service_changed` | |
| An in-flight GATT operation when the link drops | | ✅ | ✅ `gatt_disconnect_purge` | |
| Pairing / bonding (central) | | ✅ | ✅ `security_bond` | ✅ `security` |
| Static passkey / MITM / authenticated attribute | | ✅ | ✅ `security_passkey` | ✅ `security` |
| Runtime Passkey Entry | | ✅ | ✅ `runtime_passkey` | planned `security` |
| Numeric Comparison (confirm / reject / timeout) | | ✅ | ✅ `numeric_comparison` | planned (inside `security`) |
| Peripheral connection snapshot / security events | | | **missing** (API not implemented) | |
| Lifecycle repetition / heap / task / event leaks | | ✅ | **missing** → `lifecycle_stress` | |
| Wi-Fi / BLE coexistence (shared on-chip radio) | | ✅ | **missing** → `wifi_ble_coexistence` | |
| PHY update | — | — | **out of scope** (Bluetooth 4.2 LE; no 2M/Coded PHY) | |
| Persistent subscriptions / auto-reconnect | — | — | **out of scope** (API not offered; documented as an EspBle difference) | |
| Multiple simultaneous connections | — | — | **out of scope** (one central and one peripheral link) | |

### Standard GATT profiles (one-to-one with the examples)

**Every peer cell is currently missing**: the profile sketches under
[examples](../examples/) are compile-verified only. The rows stay in the table
marked "missing" so that the unverified wire format is visible.

| Profile | unit | build | peer | interop |
|---|---|---|---|---|
| Battery Service | | ✅ | missing `battery_service` | |
| Device Information Service | | ✅ | missing `device_information` | |
| Current Time / Reference Time Update | | ✅ | missing `current_time` / `reference_time_update` | |
| Heart Rate | | ✅ | missing `heart_rate` | planned `profile_wire` |
| Health Thermometer | ✅ `unit/medical_float` | ✅ | missing `health_thermometer` | planned `profile_wire` |
| Blood Pressure | ✅ `unit/medical_float` | ✅ | missing `blood_pressure` | |
| Pulse Oximeter | ✅ `unit/medical_float` | ✅ | missing `pulse_oximeter` | |
| Weight Scale / Body Composition | | ✅ | missing `weight_scale` / `body_composition` | |
| Glucose (RACP procedure) | ✅ `unit/medical_float` | ✅ | missing `glucose` | |
| Continuous Glucose Monitoring | ✅ `unit/cgm_crc` | ✅ | missing `continuous_glucose_monitoring` | planned `profile_wire` |
| Environmental Sensing | | ✅ | missing `environmental_sensing` | |
| Cycling Speed and Cadence / Power | | ✅ | missing `cycling_speed_cadence` / `cycling_power` | |
| Running Speed and Cadence | | ✅ | missing `running_speed_cadence` | |
| Fitness Machine (FTMS) | | ✅ | missing `fitness_machine` | |
| Location and Navigation | | ✅ | missing `location_navigation` | |
| User Data | | ✅ | missing `user_data` | |
| Alert Notification / Immediate Alert / Phone Alert Status | | ✅ | missing `alert_notification` / `immediate_alert` / `phone_alert_status` | |
| Proximity (Link Loss + Tx Power) | | ✅ | missing `proximity` | |
| Bond Management | | ✅ | missing `bond_management` | |

### HID / MIDI (not implemented; [Phase 1](../docs/PROFILE_BRIDGE_ROADMAP.ja.md))

These are missing implementations, not backend impossibilities. Each is enabled
once its prerequisites are met.

| Area | unit | build | peer | interop |
|---|---|---|---|---|
| HID report map parser | ✅ `unit/report_map` | — | — | |
| Keyboard layout / keymap | ✅ `unit/keymap` | — | — | |
| BLE MIDI packet codec | ✅ `unit/midi` | — | — | |
| Multi-observer dispatch (`add*Listener()`) | | planned | planned `multi_listener` | |
| BLE MIDI device / host | codec above | planned | planned `midi_device` / `midi_host` | planned `interop/midi` |
| HID device (keyboard / mouse / consumer / system / gamepad / vendor) | parser above | planned | planned `hid_keyboard_device`, `hid_robustness`, `hid_security`, `hid_boot_protocol`, `hid_custom`, `hid_convenience` | planned `interop/hid` |
| HID host | parser above | planned | planned `hid_keyboard_host`, `hid_boot_keyboard`, `hid_keyboard_nkro` | planned `interop/hid` |

### Bluetooth Classic / dual mode (specific to this library)

| Area | unit | build | peer |
|---|---|---|---|
| Capability / profile support reasons | | ✅ | ✅ inside `classic_inquiry` |
| Inquiry (name / CoD / RSSI / stop / complete) | | ✅ | ✅ `classic_inquiry` |
| SPP server / client | | ✅ | ✅ `spp_server` / `spp_client` |
| SPP RX ring (binary / overflow / invalidation) | | ✅ | ✅ `spp_receive_buffer` |
| SPP stream wrapper | | ✅ | ✅ `spp_serial` |
| Multiple SPP sessions (raw feasibility) | | — | ✅ `spp_multi_backend` |
| Classic security (SSP / passkey / bond store) | | ✅ | ✅ `spp_security` / `spp_passkey` |
| A2DP sink / source + AVRCP | | ✅ | ✅ `a2dp_sink` / `a2dp_source` |
| A2DP long soak (underrun / heap / latency) | | — | **missing** → `a2dp_soak` |
| HFP HF / AG (SLC / SCO / CVSD / mSBC) | | ✅ | ✅ `hfp_backend` |
| BLE + SPP dual mode | | ✅ | ✅ `dual_mode_scan_spp` |
| Cross-profile resource conflicts (A2DP + SPP, …) | | — | **missing** → `profile_resource_conflict` |
| Classic session APIs match EspBle vocabulary | ✅ `unit/api_parity` | ✅ | added as an objective of the existing suites |

## Implemented scenarios

Current state: 27 peer suites / 33 test functions, 5 unit test functions.

1. ✅ `stack_smoke`: two boards connected through the bundled Arduino-ESP32 API —
   GATT read/write, CCCD subscription, notification.
2. ✅ `advertise_scan`: public lifecycle, independent advertising and scan
   response payloads, active-scan merge with Service Data / Appearance / Tx
   Power, payload-overflow rejection, value-type results, duration and explicit
   stop, deterministic 16-entry queue overflow accounting, `end()` flush,
   reinitialization.
3. ✅ `advertise_payload`: raw AD structures, grouped UUIDs, the 31-byte
   boundary, timed stop.
4. ✅ `ibeacon`: shared codec — encode → broadcast → scan → decode.
5. ✅ `connect_disconnect`: non-blocking connect, reconnect IDs, 23 → negotiated
   MTU exchange, HCI disconnect reasons, timeout classification against a
   non-advertising peer, `end()` both in flight and established, peer
   disconnection, reinitialization.
6. ✅ `connection_parameters`: initial snapshot, central update request, matching
   negotiated values on both peers, `update()` dispatch.
7. ✅ `local_identity`: Random Static / RPA, reported address matching the
   observed one, −12/+9 dBm and the Tx Power Level on air.
8. ✅ `accept_list`: pre-initialization rejection, idempotent entries,
   controller-side rejection of an unlisted central, `acceptListOnly` on the scan
   side, connection and disconnection after switching to `Any`.
9. ✅ `directed_advertising`: payload-free High Duty packet addressed to the
   central, connect and disconnect, the 1.28-second limit, Low Duty persistence
   and explicit stop.
10. ✅ `gatt_client`: connection-scoped database snapshot, single-characteristic
    discovery, UUID- and handle-addressed characteristic operations, descriptor
    read/write, subscribe/unsubscribe, binary-safe values, invalidation on
    disconnect, `update()` dispatch.
11. ✅ `gatt_server`: static server, dynamic read, binary write, descriptor
    write, CCCD subscription, notification, send completion, `update()` dispatch.
12. ✅ `security_bond`: Just Works, encrypted GATT, bond storage, encrypted
    reconnect, security callbacks, bond removal.
13. ✅ `security_passkey`: static-passkey MITM, passkey display, authenticated
    GATT, bond storage.
14. ✅ `runtime_passkey`: KeyboardOnly runtime entry, `disconnect()` / `end()`
    while input is pending, unanswered timeout, immediate retry,
    reinitialization.
15. ✅ `numeric_comparison`: matching six digits with DisplayYesNo, explicit
    rejection without dropping the link, unanswered timeout, retry.
16. ✅ `classic_inquiry`: dual-mode initialization, compile-time capability
    snapshot, Classic name / Class of Device / RSSI, stopping from a result
    callback, completion delivered from `update()`.
17. ✅ `spp_server`: binary-safe bidirectional data against a raw ESP-IDF client,
    reconnect IDs, remote disconnection, `end()` while running, ordered 8-entry
    write queue with overflow, completion for all eight accepted writes.
18. ✅ `spp_client`: asynchronous SDP/RFCOMM connect, shared session API, binary
    data, write completion, local disconnect, reconnect IDs, failure/timeout
    delivery.
19. ✅ `spp_receive_buffer`: 2048-byte RX ring, binary read, overflow byte count,
    invalidation on disconnect.
20. ✅ `spp_serial`: root-bound `EspBluedroidSppSerial`, automatic follow across
    two consecutive sessions, `Stream`/`Print`, 1000-byte split write, `flush()`,
    invalidation after disconnect.
21. ✅ `spp_security`: DisplayYesNo SSP in both client and server roles, explicit
    rejection, retry after authentication failure, Classic bond enumeration /
    reconnect / removal, authenticated and encrypted data.
22. ✅ `spp_passkey`: Classic DisplayOnly / KeyboardOnly in both directions,
    unanswered timeout, rejection of late input, retry, `end()` while input is
    pending, reinitialization with inverted I/O capability.
23. ✅ `spp_multi_backend`: raw Bluedroid feasibility — two simultaneous sessions
    on two RFCOMM channels over one ACL, handle-separated data, both closed.
24. ✅ `a2dp_sink` / `a2dp_source`: public A2DP against a raw counterpart — PCM,
    AVRCP Play/Pause press and release, absolute volume, callback context (PCM on
    the stack task, control from `update()`), disconnect and shutdown.
25. ✅ `hfp_backend`: SLC, SCO, and bidirectional CVSD / mSBC mono PCM between the
    public Hands-Free and Audio Gateway roles, disconnection.
26. ✅ `dual_mode_scan_spp`: BLE scan, connect, discovery, and read/write during
    an active SPP session; 64 → 128 → 256 notifications on one link; per-round
    BLE event drop accounting; delivered notifications round-tripped over SPP;
    GATT completion prioritized when the queue is full.
27. ✅ Host unit tests: `uuid`, `codec`, `ibeacon`, `medical_float`, `cgm_crc`.

## Priorities

Start with the gaps that need no implementation work.

**P1 — closable with zero implementation**

- `unit/api_parity` plus `docs/API_PARITY.tsv` (turn the EspBle diff into a
  machine check)
- `unit/report_map` / `unit/keymap` / `unit/midi` (port the headers from EspBle;
  the foundation for HID / MIDI)
- Add a **real indication** to `gatt_server` (today only the flag is printed)
- `duplicate_uuid` (regression for the current rejection contract and its error
  string; invert this test when the restriction is lifted)
- `gatt_queue_purge` (deferred completion of the in-flight operation and failure
  delivery for queued ones on disconnect)
- `service_changed`
- `long_value` (pin the read-truncation contract above the MTU)

**P2 — gaps in shipped implementations**

- 24 standard GATT profile peer tests (one-to-one with the examples, sharing
  EspBle's wire expectations)
- `lifecycle_stress`
- `wifi_ble_coexistence` (the original ESP32 shares one radio)
- `a2dp_soak`, `profile_resource_conflict`

**P3 — requires new foundation APIs**

- Peripheral connection snapshot / security events → security server scenarios,
  resolving the asymmetry in [examples/Security](../examples/Security/), and the
  prerequisite for a HID device
- `add*Listener()` → `multi_listener`

**P4 — profile implementations**

- BLE MIDI device / host (shortest path once the P1 codecs and `add*Listener()`
  are in)
- BLE HID device (needs the duplicate-characteristic-UUID restriction lifted) →
  HID host

**interop**: each layer moves into `interop/` once its API and wire behaviour
settle. `gatt_basic`, `advertise_scan`, `long_value`, `duplicate_uuid`,
`security` and `profile_wire` are done. What remains is Numeric Comparison inside
`security`, the reverse direction of the connection-oriented scenarios (waiting
for the peripheral connection snapshot), and the HID/MIDI pair.

## Pass criteria

- The test code generates every input and decides the verdict from serial
  assertions.
- Pass criteria including timeouts and retries are fixed.
- Coverage is not public-API-to-public-API only; each area also runs against the
  bundled Arduino-ESP32 API or a raw ESP-IDF implementation.
- Scenarios that share an EspBle name use the same wire expectations, and every
  difference appears in `docs/API_PARITY.tsv` with a reason.
- Items that need manual confirmation are never mixed into automated pass
  criteria.
- A bare `pytest` completes with the two permanently connected boards alone.
