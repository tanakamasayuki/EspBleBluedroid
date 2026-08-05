import re

# What the device side of either stack is configured with, so a discovery result
# has to have been read from the peer's attributes rather than assumed.
COUNTRY_CODE = 33
BATTERY_LEVEL = 73
KEYBOARD_REPORT_ID = 1
# Shift+A, and what any HOGP host must make of it: usage 0x04 with the left shift
# modifier, which the layout turns into 'A'. The report is the boot-compatible 8
# bytes in both libraries.
USAGE_A = 0x04
LEFT_SHIFT = 0x02
MODIFIER_LEFT_SHIFT_USAGE = 0xe1
REPORT_LENGTH = 8

DISCOVERED = (
    rb"DISCOVERED success=(\d+) report=(\d+) country=(\d+)/(\d+) output=(\d+) "
    rb"battery=(\d+)/(\d+)"
)
KEY = (
    rb"KEY usage=(\d+) ascii=(\d+) pressed=(\d+) released=(\d+) mods=0x(\w+) "
    rb"length=(\d+)"
)


def check_discovery(host, prefix):
    """The host's account of the peer, which must match the peer's configuration."""
    match = host.expect(re.compile(prefix.encode() + b"_" + DISCOVERED), timeout=40)
    assert match.group(1) == b"1", "discovery across the two stacks failed"
    assert int(match.group(2)) == KEYBOARD_REPORT_ID, (
        "the keyboard's Report ID is read from the Report Reference descriptors"
    )
    assert match.group(3) == b"1" and int(match.group(4)) == COUNTRY_CODE, (
        "the country code is read from HID Information"
    )
    assert match.group(5) == b"1", "the keyboard's Output Report was found"
    assert match.group(6) == b"1" and int(match.group(7)) == BATTERY_LEVEL, (
        "the battery level is read from the Battery Service"
    )


def check_keystroke(sender, host, send_prefix, host_prefix, raw_length=True):
    """One keystroke, encoded by one library and decoded by the other.

    `raw_length` is False for EspBle 1.1.0 as the host: it leaves `rawLength` 0 on a
    decoded keyboard event, where this library carries the report it decoded from.
    The shared expectation therefore cannot include it — everything the two stacks
    must agree on is asserted for both.
    """
    sender.write("a")
    sender.expect_exact("%s_SEND press=1 error=NONE" % send_prefix, timeout=25)
    # The key itself, then the modifier as a usage of its own: a modifier is both a
    # bit in the report and a usage, and both stacks report it the same way.
    key = host.expect(re.compile(host_prefix.encode() + b"_" + KEY), timeout=30)
    assert int(key.group(1)) == USAGE_A
    assert int(key.group(2)) == ord("A"), (
        "with shift held the layout produces 'A' on either stack"
    )
    assert (key.group(3), key.group(4)) == (b"1", b"0")
    assert int(key.group(5), 16) == LEFT_SHIFT
    if raw_length:
        assert int(key.group(6)) == REPORT_LENGTH, "the 6KRO report is 8 bytes"
    modifier = host.expect(re.compile(host_prefix.encode() + b"_" + KEY), timeout=30)
    assert int(modifier.group(1)) == MODIFIER_LEFT_SHIFT_USAGE
    assert modifier.group(2) == b"0", "a modifier produces no character"

    sender.write("r")
    sender.expect_exact("%s_SEND release=1 error=NONE" % send_prefix, timeout=25)
    released = host.expect(re.compile(host_prefix.encode() + b"_" + KEY), timeout=30)
    assert released.group(4) == b"1", "releasing must be reported as a release"


def wait_idle(board, prefix):
    """Prove the board is up and still in no mode, on request.

    Nothing may be expected from boot output here: the two boards are flashed one
    after the other, so whatever the first one printed while the second was being
    flashed is already gone (tests/TEST_PLAN.md).
    """
    # '0' resets the role rather than only reporting it: pytest reflashes a board
    # only when its binary changed, so the second test of a run can start with the
    # first test's mode still in place.
    board.write("0")
    board.expect_exact("%s_STATE mode=- id=0 ready=0" % prefix, timeout=30)


def connect_and_discover(host, device, host_prefix, device_prefix):
    device.expect_exact("%s_DEVICE_READY" % device_prefix, timeout=30)
    host.expect_exact("%s_HOST_READY" % host_prefix, timeout=30)
    host.write("s")
    host.expect_exact("%s_SCAN started=1" % host_prefix, timeout=20)
    host.expect_exact("%s_FOUND" % host_prefix, timeout=30)
    host.expect(re.compile(host_prefix.encode() + rb"_CONNECTED id=(\d+)"),
                timeout=30)
    host.write("D")
    host.expect_exact("%s_DISCOVER accepted=1" % host_prefix, timeout=20)
    check_discovery(host, host_prefix)
    # Discovery ends with every Input Report subscribed, which is what makes the
    # device ready to send on the other side.
    device.write("?")
    device.expect(
        re.compile(device_prefix.encode() + rb"_STATE mode=d id=\d+ ready=1"),
        timeout=25,
    )


def test_bluedroid_hid_host_against_an_espble_hid_device(dut, peers):
    """This library's HID Host against EspBle's HID Device.

    The descriptor bytes are already known to be identical and both sides parse them
    with the same helper; what this adds is that a *different implementation* on the
    air reaches the same conclusions from them — the Report References name the same
    reports, the keystroke decodes to the same usage and character, and the LED write
    goes back the other way.
    """
    espble = dut
    bluedroid = peers["device"]
    wait_idle(espble, "ESPBLE")
    wait_idle(bluedroid, "BLUEDROID")

    espble.write("d")
    bluedroid.write("h")
    connect_and_discover(bluedroid, espble, "BLUEDROID", "ESPBLE")

    check_keystroke(espble, bluedroid, "ESPBLE", "BLUEDROID")

    # The one report a keyboard host writes, in the other direction.
    bluedroid.write("l")
    bluedroid.expect_exact("BLUEDROID_LEDS accepted=1", timeout=20)
    espble.expect_exact("ESPBLE_LED leds=0x02 caps=1", timeout=25)


def test_espble_hid_host_against_a_bluedroid_hid_device(dut, peers):
    """EspBle's HID Host against this library's HID Device — the same scenario with
    the roles swapped, so neither stack is only ever the one that decodes."""
    espble = dut
    bluedroid = peers["device"]
    wait_idle(espble, "ESPBLE")
    wait_idle(bluedroid, "BLUEDROID")

    bluedroid.write("d")
    espble.write("h")
    connect_and_discover(espble, bluedroid, "ESPBLE", "BLUEDROID")

    check_keystroke(bluedroid, espble, "BLUEDROID", "ESPBLE", raw_length=False)

    espble.write("l")
    espble.expect_exact("ESPBLE_LEDS accepted=1", timeout=20)
    bluedroid.expect_exact("BLUEDROID_LED leds=0x02 caps=1", timeout=25)
