# Unit Tests

> 日本語版: [README.ja.md](README.ja.md)
> Where these fit: [../TEST_PLAN.md](../TEST_PLAN.md)

Pure C++/data conversion tests that run on the host with g++. No boards or
serial ports are required.

```sh
uv run --env-file .env pytest unit/
```

Each suite is one directory holding the C++ program and the pytest wrapper that
compiles it with `-Wall -Wextra -Werror` and runs it. A non-zero exit status
fails the test.

## Suites

- `uuid`: verifies the UUID codec in `src/EspBleUuid.h` — 16/32/128-bit parsing
  and formatting, short forms, and equivalence with the Bluetooth Base UUID.
- `codec`: verifies the Bluedroid-specific conversions in
  `src/internal/EspBleBluedroidCodec.cpp` and the GATT client link state machine
  in `src/internal/EspBleBluedroidGattcState.cpp`. This suite has no EspBle
  counterpart because it covers backend-internal state.
- `ibeacon`: verifies the iBeacon codec in `src/EspBleIBeacon.h` — encode and
  decode of the manufacturer payload with every field.
- `medical_float`: verifies the IEEE-11073 medical float codec in
  `src/EspBleMedicalFloat.h` — 32-bit FLOAT and 16-bit SFLOAT encode/decode round
  trips, exact little-endian byte layout, negative mantissas, and the reserved
  NaN / NRes / ±INFINITY values used by Health Thermometer, Blood Pressure, and
  Glucose.
- `cgm_crc`: verifies the CGM E2E-CRC codec in `src/EspBleCgmCrc.h`
  (CRC-16/MCRF4XX) — the documented check value 0x6f91 for "123456789", the
  empty-input initial value, append/verify round trips over a representative CGM
  Measurement, corruption detection, and rejection of values too short to hold a
  CRC.
- `keymap`: verifies the HID usage → character conversion in
  `src/EspBleKeymap.h` (`espBleUsageToUnicode` / `espBleUsageToAscii`) against
  expected values derived from the primary sources (Windows layout data) for each
  layout. Pins down AltGr layer selection and fallback, character-pair Caps Lock
  handling, dead keys returning 0, and `ascii` = 0 for non-Latin-1 characters.
- `report_map`: verifies the HID Report Map parser in
  `src/EspBleHidReportMap.h` — keyboards with reordered descriptor items, boot
  keyboards without a report ID, keyboards with an additional Consumer Control
  report, mouse-only descriptors, and truncated descriptors.
- `midi`: verifies the BLE MIDI packet codec in `src/EspBleMidi.h` — timestamp
  header/low-byte decoding, running status (with and without the timestamp byte),
  System Real-Time interleaving, System Exclusive within one packet and across
  two, malformed packets, the packet builder, and the multi-packet SysEx encoder.
- `api_parity`: compares the public API surface of `src/EspBleBluedroid.h`
  against the pinned EspBle snapshot (`espble.symbols`) and fails on any
  difference that `docs/API_PARITY.tsv` does not classify with a reason. It also
  compares what the `*Name()` functions *return* — the enum-to-string maps in
  `src/EspBleBluedroid.cpp` against `espble.values` — because two libraries can
  agree on every signature and still hand the application different strings, which
  no header shows. Finally it holds `src/EspBleMidiProfile.h` to being EspBle's
  file with one type renamed: both sides are reduced to their code lines,
  `EspBleBluedroid` becomes `EspBle`, and the result must equal
  `espble.midi_profile`. This suite reads the three committed snapshots, so it
  needs no EspBle checkout. See
  [../TEST_PLAN.md](../TEST_PLAN.md#pinning-espble-api-agreement-with-tests).

`keymap`, `report_map`, and `midi` cover headers that are verbatim copies of
EspBle's, so the test programs are the same as EspBle's too. They are the
foundation for the BLE MIDI profile, which is now implemented on top of them
(`src/EspBleMidiProfile.h`), and for HID over GATT, which is not.
