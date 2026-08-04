# EspBle interoperability tests

> 日本語版: [README.ja.md](README.ja.md)
> Rules and scenario list: [../TEST_PLAN.md](../TEST_PLAN.md#espble-release-package-interop-suite-interop)

Cross-stack tests: this library (Bluedroid, original ESP32) against the sibling
library [EspBle](https://github.com/tanakamasayuki/EspBle) (NimBLE, ESP32-S3).

Bluedroid talking to Bluedroid cannot reveal an implementation that leans on
Bluedroid's own behaviour, because both ends make the same assumption and it
cancels out. These scenarios put a different host stack on the other end.

## Fixture

| Fixture | Board | Profile | Firmware |
|---|---|---|---|
| `dut` | ESP32-S3 | `s3_peer_host` | a released EspBle, pinned in `sketch.yaml` |
| `peers["device"]` | original ESP32 | `esp32_peer_device` | this repository |

EspBle does not run on the original ESP32, so its half always needs a
NimBLE-capable SoC. The S3 — EspBle's own main board — is permanently connected
here, and the peer is the same second ESP32 the `peer/` suites use. No wiring
between boards is needed, only serial and power.

## Setup

Nothing to download by hand: the EspBle version is pinned in each interop
`sketch.yaml` and Arduino CLI installs exactly that release.

```yaml
    libraries:
      - EspBle (1.1.0)
```

The S3's port belongs in `tests/.env` with the other two boards, and a bare
`pytest` includes this suite:

```dotenv
TEST_SERIAL_PORT_S3_PEER_HOST=/dev/ttyACM0
```

```sh
uv run --env-file .env pytest interop/
```

## Rules

- The peer firmware is a **released** EspBle from the Arduino library index,
  never `../EspBle`, a default branch, or an unreleased commit. What these tests
  verify is agreement with a version someone can actually install.
- The version lives in `sketch.yaml` and is bumped by the separate release
  tooling, as an explicit change: review the diff and re-run the whole suite.
- Never patch the installed package to make a scenario pass. If a scenario only
  works with a change on the EspBle side, that belongs in the result.
- Only scenarios pytest can build, flash, drive, assert, time out, and clean up
  unattended. Phone interaction, GUI checks, and listening tests belong to the
  manual interoperability section of the release checklist.

## Scenarios

| Scenario | Content |
|---|---|
| [gatt_basic](gatt_basic/) | Bluedroid central against an EspBle peripheral: MTU 247 exchange, discovery including declared properties, characteristic read, write with and without response, descriptor read/write, notification, indication with its confirmation, unsubscribe, disconnect |
| [advertise_scan](advertise_scan/) | Both directions of advertising and scanning: name, manufacturer data, Service Data, Appearance and Tx Power built by one stack's payload builder and reconstructed by the other's parser, plus a passive scan of the same advertiser that must see the advertising payload alone |
| [long_value](long_value/) | A 300-byte value published by an EspBle peripheral read whole across the 247-byte MTU, through both the UUID form and the handle form, with every byte checked against the peer's ramp |
| [duplicate_uuid](duplicate_uuid/) | Two characteristics sharing one UUID in one service on the EspBle peripheral: discovery keeps them apart, the UUID form reaches the first, and read, write, subscribe and notification are each attributed to a handle on both sides. This library's server-side rejection of the same shape is recorded alongside |
| [security](security/) | Just Works, static-passkey Passkey Entry, and Numeric Comparison (confirmed and refused): encrypted / authenticated / bonded / key size asserted on both sides, the bond recorded by both, the attribute tiers exercised on each link type, the same six digits derived by both implementations, and nothing left behind after a refusal |
| [profile_wire](profile_wire/) | The shared codec headers across the two libraries: a FLOAT32 read and notified, a CGM E2E-CRC appended by one copy and verified by the other, an SFLOAT written back, and an iBeacon decoded from the advertisement. Roles reversed — the library under test is the GATT server and the beacon |

The remaining planned scenarios — the reverse direction of the connection-oriented
scenarios and the HID/MIDI pair — are listed with their
content in [../TEST_PLAN.md](../TEST_PLAN.md#scenarios-added-as-each-layer-settles).

## UUIDs

Interop scenarios take suite tags from the `01xx` range of the test UUID scheme
(`SSSSNNNN-b1dd-4d00-9e5a-627564726f69`), so they cannot collide with either
library's own suites even when both run in the same room. `gatt_basic` uses
`0100`, `advertise_scan` uses `0101` (one UUID per direction, so neither scanner
can be satisfied by the other side's payload), `long_value` uses `0102`,
`duplicate_uuid` uses `0103`, `security` uses `0104`, and `profile_wire` uses
`0105` (its iBeacon payload carries the `0105 0100` UUID, so the beacon scan
cannot be satisfied by another beacon in the room).

## Reading the logs

The EspBle side prefixes its output `ESPBLE_`, so no log line leaves it ambiguous
which stack produced it.

## Fixture notes

- `Serial` on the S3 stays on UART0 (board defaults). This fixture reaches the S3
  through a CH9102 USB-serial bridge, so enabling "USB CDC On Boot" would move the
  output to the native USB port and the board would look silent while running
  perfectly.
- The EspBle side answers a `?` status request instead of being waited on at boot.
  It finishes booting while the other board is still being flashed, so an
  assertion on its startup line alone would depend on when the monitor started
  reading.
