import re
from pathlib import Path

MOUSE_BUTTONS = 3
# The snapshot of EspBle's descriptor tables, shared with tests/unit/hid_report_maps.
# Reading it here is deliberate: it makes this test assert the *composition rule*
# end to end — EspBle's bytes, in profile order, with the configurable fields
# patched — rather than restating a long hex string that nobody can check by eye.
SNAPSHOT = Path(__file__).parent / ".." / ".." / "unit" / "hid_report_maps" / \
    "espble.hid_maps"


def descriptor_tables():
    tables = {}
    for line in SNAPSHOT.read_text().splitlines():
        if line.startswith("#") or not line.strip():
            continue
        name, value = line.split("\t")
        tables[name] = bytearray.fromhex(value)
    return tables


def expected_report_map():
    """Keyboard, mouse, gamepad, consumer, system — in profile order."""
    tables = descriptor_tables()
    mouse = tables["mouseMap"]
    # The three offsets the mouse button count is patched into: the button Usage
    # Maximum, its Report Count, and the Report Size of the padding that follows,
    # so the report stays one byte wide whatever the count.
    mouse[17] = MOUSE_BUTTONS
    mouse[23] = MOUSE_BUTTONS
    mouse[31] = 8 - MOUSE_BUTTONS
    composed = (
        tables["keyboardMap"] + mouse + tables["gamepadMap"]
        + tables["consumerMap"] + tables["systemMap"]
    )
    return composed.hex()


def test_five_profiles_share_one_hid_service(dut, peers):
    """Keyboard, mouse, consumer, system and gamepad in one HID service.

    HOGP gives a device one HID service and one Report Map, and tells the reports
    apart by Report ID. So the things worth checking are the ones only a composite
    device can get wrong: that the published Report Map is the five descriptors
    concatenated in profile order with the mouse button count patched in, that
    there are five Input Report characteristics sharing UUID 0x2A4D each with its
    own Report Reference, and that a notification arrives on the handle belonging
    to the profile that sent it.
    """
    peer = peers["device"]
    dut.write("?")
    dut.expect_exact(
        "READY_STATE started=1 configured=1 keyboard=0 mouse=0 consumer=0 "
        "system=0 gamepad=0",
        timeout=30,
    )
    peer.expect_exact("HID_KEYBOARD_PEER_READY", timeout=30)

    peer.write("c")
    peer.expect_exact("PEER_CONNECTED hid=1", timeout=40)

    # One Report Map for the whole device: EspBle's tables, in profile order, with
    # the mouse button count patched.
    expected = expected_report_map()
    peer.write("m")
    peer.expect_exact(
        "PEER_REPORT_MAP length=%d hex=%s" % (len(expected) // 2, expected),
        timeout=30,
    )

    # Five Input Reports and the keyboard's Output Report, all 0x2A4D, told apart
    # only by their Report Reference: report id : type, in handle order.
    # The attribute order is the order the profiles were configured in, which is
    # not the descriptor order inside the Report Map (that one is profile order).
    # Both are checked, because a host uses the Report Reference rather than either.
    peer.write("d")
    peer.expect_exact(
        "PEER_REPORTS count=6 1:1 1:2 2:1 4:1 5:1 3:1", timeout=30
    )
    peer.write("R")
    handles = peer.expect(
        re.compile(rb"PEER_HANDLES count=6 (.+)"), timeout=25
    )
    handle_of = {}
    for item in handles.group(1).decode().split():
        report, handle = item.split("=")
        handle_of[report] = int(handle)
    assert len(set(handle_of.values())) == 6, (
        "six reports need six handles: %s" % handle_of
    )

    peer.write("s")
    peer.expect_exact("PEER_SUBSCRIBED count=5", timeout=40)
    # Every profile becomes ready at once, because each has its own CCCD and the
    # host subscribed to all five.
    dut.write("?")
    dut.expect_exact(
        "READY_STATE started=1 configured=1 keyboard=1 mouse=1 consumer=1 "
        "system=1 gamepad=1",
        timeout=25,
    )

    # Each profile's report, on its own handle, with the exact wire bytes.
    dut.write("k")
    dut.expect_exact("SEND keyboard=1 error=NONE", timeout=20)
    peer.expect_exact(
        "PEER_INPUT handle=%d length=8 hex=0200040000000000" % handle_of["1:1"],
        timeout=25,
    )

    # Mouse: the right button goes down, then a move that keeps it down — buttons,
    # x, y, wheel as signed bytes.
    dut.write("m")
    dut.expect_exact("SEND mouse=1 error=NONE", timeout=20)
    peer.expect_exact(
        "PEER_INPUT handle=%d length=4 hex=02000000" % handle_of["2:1"], timeout=25
    )
    dut.expect_exact("SEND move=1 buttons=0x02", timeout=20)
    peer.expect_exact(
        "PEER_INPUT handle=%d length=4 hex=0205fb01" % handle_of["2:1"], timeout=25
    )
    dut.write("M")
    dut.expect_exact("SEND mouse_release=1 buttons=0x00", timeout=20)
    peer.expect_exact(
        "PEER_INPUT handle=%d length=4 hex=00000000" % handle_of["2:1"], timeout=25
    )

    # Consumer control: one 16-bit usage, little-endian. Volume Up is 0x00e9.
    dut.write("c")
    dut.expect_exact("SEND consumer=1 usage=233", timeout=20)
    peer.expect_exact(
        "PEER_INPUT handle=%d length=2 hex=e900" % handle_of["4:1"], timeout=25
    )
    dut.write("C")
    dut.expect_exact("SEND consumer_release=1 usage=0", timeout=20)
    peer.expect_exact(
        "PEER_INPUT handle=%d length=2 hex=0000" % handle_of["4:1"], timeout=25
    )

    # System control: a single usage byte.
    dut.write("s")
    dut.expect_exact("SEND system=1 usage=2", timeout=20)
    peer.expect_exact(
        "PEER_INPUT handle=%d length=1 hex=02" % handle_of["5:1"], timeout=25
    )

    # Gamepad: six signed axes, the hat, then 32 button bits little-endian.
    dut.write("g")
    dut.expect_exact("SEND gamepad=1 error=NONE", timeout=20)
    peer.expect_exact(
        "PEER_INPUT handle=%d length=11 hex=01fe03fc05fa0303020100"
        % handle_of["3:1"],
        timeout=25,
    )

    # The keyboard's Output Report is still the one a host writes for the LEDs, and
    # it is the only writable report here.
    peer.write("o")
    peer.expect_exact("PEER_OUTPUT_WRITTEN", timeout=25)

    peer.write("x")
    peer.expect_exact("PEER_DISCONNECT_REQUESTED", timeout=20)
