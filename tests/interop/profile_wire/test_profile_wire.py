import re

# 36.50 °C as FLOAT32 (mantissa 3650, exponent -2), little-endian behind the
# Health Thermometer flags octet.
TEMPERATURE_HEX = "00420e00fe"
TEMPERATURE_MILLI = 36500
# CGM Measurement: size, flags, SFLOAT 123.4 (mantissa 1234, exponent -1), time
# offset 42, then the little-endian E2E-CRC.
GLUCOSE_HEX = "0803d2f42a00"
GLUCOSE_MILLI = 123400
# 9.87 as SFLOAT (mantissa 987, exponent -2) behind a units flag, encoded by the
# EspBle side.
WRITTEN_MILLI = 9870


def test_shared_codec_values_cross_the_stacks_unchanged(dut, peers):
    """Values built with the shared codec headers, decoded by the other library.

    `EspBleMedicalFloat.h`, `EspBleCgmCrc.h` and `EspBleIBeacon.h` are verbatim
    copies of EspBle's, and `unit/` checks each copy against its own vectors. That
    cannot catch two copies drifting apart in the same direction as their tests:
    only a round trip can, where one library encodes, the bytes cross the air, and
    the *other* library's compiled copy decodes.

    So every value is asserted twice — as the exact bytes on the wire and as the
    decode, in integer milli-units so the comparison never depends on float
    formatting. A wrong endianness or a mis-packed SFLOAT exponent changes the
    hex; a decoder that drifted changes the milli value.

    This is also the first interop scenario with the roles the other way round:
    `peers["device"]` (the library under test) is the GATT server and then the
    beacon, and `dut` (EspBle on the ESP32-S3) is the central and the decoder.
    """
    espble = dut
    bluedroid = peers["device"]

    # Both boards are asked to report rather than waited on: each finishes booting
    # while the other is still being flashed.
    espble.write("?")
    espble.expect_exact("ESPBLE_PROFILE_WIRE_STATE ready=1", timeout=40)
    bluedroid.write("?")
    bluedroid.expect_exact("PROFILE_WIRE_STATE ready=1", timeout=40)

    espble.write("c")
    espble.expect_exact("ESPBLE_SCAN_STARTED 1", timeout=10)
    espble.expect(
        re.compile(rb"ESPBLE_TARGET_FOUND address=([0-9a-f:]+)"), timeout=40
    )
    espble.expect_exact("ESPBLE_CONNECT_REQUESTED 1", timeout=10)
    espble.expect(re.compile(rb"ESPBLE_CONNECTED id=\d+"), timeout=25)

    espble.write("d")
    espble.expect_exact("ESPBLE_DISCOVERY_REQUESTED 1", timeout=10)
    discovery = espble.expect(
        re.compile(rb"ESPBLE_DISCOVERY success=1 characteristics=(\d+)"),
        timeout=30,
    )
    assert int(discovery.group(1)) >= 3, "the three profile characteristics"

    # A FLOAT32 built by this library, decoded by the released EspBle copy.
    espble.write("t")
    espble.expect_exact("ESPBLE_TEMPERATURE_READ_REQUESTED 1", timeout=10)
    espble.expect_exact(
        "ESPBLE_TEMPERATURE length=5 hex=%s flags=00 milli=%d"
        % (TEMPERATURE_HEX, TEMPERATURE_MILLI),
        timeout=25,
    )

    # A CGM Measurement whose E2E-CRC this library appended and the other library
    # verifies, plus the SFLOAT inside it. `crc=1` is the whole point: the
    # reflected polynomial and the byte order have to agree.
    espble.write("g")
    espble.expect_exact("ESPBLE_GLUCOSE_READ_REQUESTED 1", timeout=10)
    glucose = espble.expect(
        re.compile(
            rb"ESPBLE_GLUCOSE length=8 hex=" + GLUCOSE_HEX.encode()
            + rb"([0-9a-f]{4}) crc=(\d) milli=(\d+)"
        ),
        timeout=25,
    )
    assert glucose.group(2) == b"1", (
        "the other library's CGM codec rejected the CRC this one appended (crc "
        "bytes %s)" % glucose.group(1).decode()
    )
    assert int(glucose.group(3)) == GLUCOSE_MILLI

    # The same FLOAT32 as a notification, so the value survives the notify path
    # too and not only a read response.
    espble.write("s")
    espble.expect_exact("ESPBLE_SUBSCRIBE_REQUESTED 1", timeout=10)
    bluedroid.expect_exact("SUBSCRIPTION notifications=1 context=loop", timeout=25)
    bluedroid.write("n")
    bluedroid.expect_exact("NOTIFY_ACCEPTED 1", timeout=10)
    bluedroid.expect(
        re.compile(rb"SENT success=1 indication=0 context=loop"), timeout=25
    )
    espble.expect_exact(
        "ESPBLE_NOTIFICATION length=5 hex=%s milli=%d"
        % (TEMPERATURE_HEX, TEMPERATURE_MILLI),
        timeout=25,
    )

    # The other direction: encoded by the released EspBle copy, decoded by this
    # library's copy.
    espble.write("w")
    espble.expect_exact("ESPBLE_WRITE_REQUESTED 1", timeout=10)
    espble.expect_exact("ESPBLE_WRITE success=1", timeout=25)
    bluedroid.expect_exact(
        "WRITTEN flags=02 milli=%d length=3 context=loop" % WRITTEN_MILLI,
        timeout=25,
    )

    espble.write("x")
    espble.expect_exact("ESPBLE_DISCONNECT_REQUESTED 1", timeout=10)
    espble.expect(re.compile(rb"ESPBLE_DISCONNECTED id=\d+"), timeout=25)

    # No connection at all for the last one: the iBeacon payload is built by this
    # library and decoded by the other from the advertisement alone. The scan is
    # filtered on the encoded UUID, so another beacon nearby cannot satisfy it.
    bluedroid.write("i")
    bluedroid.expect_exact("BEACON_STARTED 1 length=25 error=NONE", timeout=20)
    espble.write("b")
    espble.expect_exact("ESPBLE_BEACON_SCAN_STARTED 1", timeout=10)
    espble.expect_exact(
        "ESPBLE_BEACON uuid=01050100b1dd4d009e5a627564726f69 major=0105 "
        "minor=2b1d power=-59",
        timeout=40,
    )
