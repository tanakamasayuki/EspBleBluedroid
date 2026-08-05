import re
from pathlib import Path

VENDOR_REPORT_SIZE = 40
CUSTOM_REPORT_ID = 7
CUSTOM_INPUT_SIZE = 4
CUSTOM_OUTPUT_SIZE = 2
CUSTOM_FEATURE_SIZE = 3
# The snapshot of EspBle's descriptor tables, shared with tests/unit/hid_report_maps
# (see tests/peer/hid_composite for why the expected map is composed here rather
# than restated as a hex string).
SNAPSHOT = Path(__file__).parent / ".." / ".." / "unit" / "hid_report_maps" / \
    "espble.hid_maps"
# The offsets the vendor report size is patched into: the Report Count of the
# Input, the Output and the Feature report.
VENDOR_SIZE_OFFSETS = (19, 25, 31)

# The sketch's own Report Descriptor, byte for byte as hid_vendor_custom.ino
# declares it. It is this test's input rather than the library's output, which is
# why it is spelled out on both sides.
CUSTOM_REPORT_MAP = bytes([
    0x06, 0x01, 0xff,
    0x09, 0x01,
    0xa1, 0x01,
    0x85, CUSTOM_REPORT_ID,
    0x15, 0x00,
    0x26, 0xff, 0x00,
    0x75, 0x08,
    0x09, 0x01,
    0x95, CUSTOM_INPUT_SIZE,
    0x81, 0x02,
    0x09, 0x02,
    0x95, CUSTOM_OUTPUT_SIZE,
    0x91, 0x02,
    0x09, 0x03,
    0x95, CUSTOM_FEATURE_SIZE,
    0xb1, 0x02,
    0xc0,
])


def descriptor_tables():
    tables = {}
    for line in SNAPSHOT.read_text().splitlines():
        if line.startswith("#") or not line.strip():
            continue
        name, value = line.split("\t")
        tables[name] = bytearray.fromhex(value)
    return tables


def expected_report_map():
    """The vendor profile's descriptor, then the sketch's own."""
    vendor = descriptor_tables()["vendorMap"]
    for offset in VENDOR_SIZE_OFFSETS:
        vendor[offset] = VENDOR_REPORT_SIZE
    return (bytes(vendor) + CUSTOM_REPORT_MAP).hex()


def test_vendor_and_custom_reports(dut, peers):
    """hidVendor() and hidCustom() in one HID service, both directions.

    These are the two profiles the library does not decode, and the only ones with
    Output and Feature reports of their own, so what matters is: one Report Map
    that is the composed vendor descriptor followed by the sketch's own; a report
    ID the vendor profile owns being refused to hidCustom(); six Report
    characteristics sharing UUID 0x2A4D whose write properties match the type each
    Report Reference declares; and every byte surviving in both directions,
    including a 40-byte report that does not fit an ATT payload at the default MTU.
    """
    peer = peers["device"]
    dut.write("?")
    dut.expect_exact(
        "READY_STATE started=1 configured=1 size=%d mtu=23 vendor=0 custom=0"
        % VENDOR_REPORT_SIZE,
        timeout=30,
    )
    # Report ID 6 belongs to the vendor profile, so hidCustom() cannot declare it.
    dut.write("k")
    dut.expect_exact("CUSTOM_CONFLICT refused=1 error=INVALID_ARGUMENT", timeout=20)
    peer.expect_exact("HID_VENDOR_CUSTOM_PEER_READY", timeout=30)

    peer.write("c")
    peer.expect_exact("PEER_CONNECTED hid=1", timeout=40)

    # One Report Map for the whole device: the built-in profile's descriptor with
    # the configured size patched in, then the descriptor the sketch supplied.
    expected = expected_report_map()
    peer.write("m")
    peer.expect_exact(
        "PEER_REPORT_MAP length=%d hex=%s" % (len(expected) // 2, expected),
        timeout=30,
    )

    # Six reports: vendor input/output/feature and custom input/output/feature.
    peer.write("d")
    peer.expect_exact("PEER_REPORTS count=6 6:1 6:2 6:3 7:1 7:2 7:3", timeout=30)
    peer.write("R")
    handles = peer.expect(re.compile(rb"PEER_HANDLES count=6 (.+)"), timeout=25)
    handle_of = {}
    for item in handles.group(1).decode().split():
        report, handle = item.split("=")
        handle_of[report] = int(handle)
    assert len(set(handle_of.values())) == 6, (
        "six reports need six handles: %s" % handle_of
    )

    # The properties have to agree with the declared type: an Input report is
    # notifiable, an Output report also takes Write Without Response, and a Feature
    # report is configuration, so it is always acknowledged.
    peer.write("p")
    peer.expect_exact(
        "PEER_PROPERTIES count=6 6:1=n1w0u0 6:2=n0w1u1 6:3=n0w1u0 "
        "7:1=n1w0u0 7:2=n0w1u1 7:3=n0w1u0",
        timeout=25,
    )

    peer.write("s")
    peer.expect_exact("PEER_SUBSCRIBED count=2", timeout=40)
    # The device's own view of the MTU, which is what decides whether a 40-byte
    # report fits: an ATT payload is MTU - 3, so a short one would truncate it.
    dut.write("?")
    state = dut.expect(
        re.compile(rb"READY_STATE started=1 configured=1 size=(\d+) mtu=(\d+) "
                   rb"vendor=(\d+) custom=(\d+)"),
        timeout=25,
    )
    assert int(state.group(1)) == VENDOR_REPORT_SIZE
    assert int(state.group(2)) >= VENDOR_REPORT_SIZE + 3, (
        "a %d-byte report needs an ATT payload that large: mtu=%s"
        % (VENDOR_REPORT_SIZE, state.group(2).decode())
    )
    assert (state.group(3), state.group(4)) == (b"1", b"1"), \
        "both profiles are subscribed, so both are ready"

    # Device to host. Every byte distinct, so a truncated report is visible as one.
    vendor_input = bytes(range(1, VENDOR_REPORT_SIZE + 1)).hex()
    dut.write("v")
    dut.expect_exact("SEND vendor=1 error=NONE", timeout=20)
    peer.expect_exact(
        "PEER_INPUT handle=%d length=%d hex=%s"
        % (handle_of["6:1"], VENDOR_REPORT_SIZE, vendor_input),
        timeout=25,
    )
    dut.write("c")
    dut.expect_exact("SEND custom=1 error=NONE", timeout=20)
    peer.expect_exact(
        "PEER_INPUT handle=%d length=%d hex=11223344"
        % (handle_of["7:1"], CUSTOM_INPUT_SIZE),
        timeout=25,
    )

    # Host to device: the direction only these two profiles have. The report ID and
    # type in each callback come from the characteristic that was written, which is
    # the only thing telling six 0x2A4D writes apart.
    peer.write("o")
    peer.expect_exact("PEER_VENDOR_OUTPUT_WRITTEN length=%d hex=%s"
                      % (VENDOR_REPORT_SIZE,
                         bytes(range(0x40, 0x40 + VENDOR_REPORT_SIZE)).hex()),
                      timeout=25)
    dut.expect_exact(
        "VENDOR_OUTPUT id=6 type=2 length=%d hex=%s context=loop"
        % (VENDOR_REPORT_SIZE, bytes(range(0x40, 0x40 + VENDOR_REPORT_SIZE)).hex()),
        timeout=25,
    )
    peer.write("f")
    peer.expect_exact("PEER_VENDOR_FEATURE_WRITTEN length=%d hex=%s"
                      % (VENDOR_REPORT_SIZE,
                         bytes(range(0x80, 0x80 + VENDOR_REPORT_SIZE)).hex()),
                      timeout=25)
    dut.expect_exact(
        "VENDOR_FEATURE id=6 type=3 length=%d hex=%s context=loop"
        % (VENDOR_REPORT_SIZE, bytes(range(0x80, 0x80 + VENDOR_REPORT_SIZE)).hex()),
        timeout=25,
    )
    peer.write("O")
    peer.expect_exact("PEER_CUSTOM_OUTPUT_WRITTEN length=2 hex=a1a2", timeout=25)
    dut.expect_exact(
        "CUSTOM_OUTPUT id=7 type=2 length=2 hex=a1a2 context=loop", timeout=25
    )
    peer.write("F")
    peer.expect_exact("PEER_CUSTOM_FEATURE_WRITTEN length=3 hex=b1b2b3", timeout=25)
    dut.expect_exact(
        "CUSTOM_FEATURE id=7 type=3 length=3 hex=b1b2b3 context=loop", timeout=25
    )

    # The declared size is the only size: a shorter vendor report is a mismatch,
    # and an undeclared custom report ID is not a report at all.
    dut.write("e")
    dut.expect_exact("SEND short_vendor=0 error=INVALID_ARGUMENT", timeout=20)
    dut.write("E")
    dut.expect_exact("SEND unknown_custom=0 error=NOT_FOUND", timeout=20)
    dut.expect_exact("READY_UNKNOWN custom9=0", timeout=20)

    peer.write("x")
    peer.expect_exact("PEER_DISCONNECT_REQUESTED", timeout=20)
