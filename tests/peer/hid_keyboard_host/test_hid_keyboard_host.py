import re

COUNTRY_CODE = 33
BATTERY_LEVEL = 73
# The device sends the boot-compatible 8-byte keyboard report.
REPORT_LENGTH = 8


def test_host_discovers_and_decodes_a_keyboard(dut, peers):
    """The HID Host side: discover a keyboard, decode its reports, write its LEDs.

    A HOGP host cannot assume a layout — it has to read the Report Map, learn which
    0x2A4D attribute carries which report from the Report Reference descriptors, and
    subscribe. This backend allows one central GATT operation at a time, so that
    whole sequence is driven by results; what is checked here is that it completes,
    that what it reports about the device matches what the device was configured
    with, and that a keystroke becomes the right usage, character and state.
    """
    device = peers["device"]
    dut.write("?")
    dut.expect_exact("READY_STATE id=0 ready=0 invalid=0", timeout=30)
    device.write("?")
    device.expect_exact("DEVICE_STATE started=1 ready=0 caps=0", timeout=30)

    # The refusals before anything is connected: an unknown connection is NotFound,
    # and an LED write with no finished discovery is InvalidState.
    dut.write("e")
    dut.expect_exact("DISCOVER_UNKNOWN accepted=0 error=NOT_FOUND", timeout=20)
    dut.expect_exact("LEDS_UNKNOWN accepted=0 error=INVALID_STATE", timeout=20)

    dut.write("s")
    dut.expect_exact("SCAN started=1", timeout=20)
    dut.expect(re.compile(rb"FOUND rssi=-?\d+"), timeout=30)
    connected = dut.expect(
        re.compile(rb"CONNECTED id=(\d+) context=(\w+)"), timeout=30
    )
    connection_id = int(connected.group(1))
    assert connected.group(2) == b"loop", "the callback must run in update()"

    # Discovery: accepted immediately, finished later — the result is the event.
    dut.write("d")
    dut.expect_exact("DISCOVER accepted=1 error=NONE", timeout=20)
    discovered = dut.expect(
        re.compile(rb"DISCOVERED success=(\d+) id=(\d+) report=(\d+) "
                   rb"country=(\d+)/(\d+) output=(\d+) battery=(\d+)/(\d+) "
                   rb"detail=\[([^\]]*)\] context=(\w+)"),
        timeout=40,
    )
    assert discovered.group(1) == b"1", (
        "discovery failed: %s" % discovered.group(9).decode()
    )
    assert int(discovered.group(2)) == connection_id
    # Everything below was read from the device's own attributes rather than assumed.
    assert discovered.group(3) == b"1", "the keyboard's Report ID is 1"
    assert discovered.group(4) == b"1" and int(discovered.group(5)) == COUNTRY_CODE, \
        "the country code comes from HID Information"
    assert discovered.group(6) == b"1", "the keyboard has an Output Report for its LEDs"
    assert discovered.group(7) == b"1" and int(discovered.group(8)) == BATTERY_LEVEL, \
        "the battery level comes from the Battery Service"
    assert discovered.group(10) == b"loop", "the callback must run in update()"

    dut.write("?")
    dut.expect_exact(
        "READY_STATE id=%d ready=1 invalid=0" % connection_id, timeout=25
    )
    # The device sees the host's subscription, which is what makes it ready to send.
    device.write("?")
    device.expect_exact("DEVICE_STATE started=1 ready=1 caps=0", timeout=25)

    # Shift+A: the host has to name the usage, apply the layout to get 'A', and
    # carry the modifier byte — none of which is in the notification as such.
    device.write("a")
    device.expect_exact("DEVICE_SEND press=1 error=NONE", timeout=20)

    # The state comes first, then one event per changed usage: a host that tracks
    # chords needs the whole state, and one that reacts to keys needs the edges.
    # The state carries the modifier usage too, so 0x04 and 0xe1 are both down.
    state = dut.expect(
        re.compile(rb"STATE mods=0x(\w+) down=([\w,]+) count=(\d+)"), timeout=25
    )
    assert int(state.group(1), 16) == 0x02, "Left Shift is modifier bit 0x02"
    down = {int(value) for value in state.group(2).decode().split(",")}
    assert down == {0x04, 0xe1}, (
        "the key and the modifier usage are both down: %s" % sorted(down)
    )

    key = dut.expect(
        re.compile(rb"KEY usage=(\d+) ascii=(\d+) pressed=(\d+) released=(\d+) "
                   rb"mods=0x(\w+) caps=(\d+) length=(\d+) context=(\w+)"),
        timeout=25,
    )
    assert int(key.group(1)) == 0x04, "usage 0x04 is the 'a' key"
    assert int(key.group(2)) == ord("A"), (
        "with shift held the layout produces 'A', not 'a'"
    )
    assert (key.group(3), key.group(4)) == (b"1", b"0"), "this is a press"
    assert int(key.group(5), 16) == 0x02, "the event carries the modifier byte"
    assert int(key.group(7)) == REPORT_LENGTH, "the 6KRO report is 8 bytes"
    assert key.group(8) == b"loop", "events must be delivered from update()"

    # The modifier is a usage of its own as well, so it gets its own event — with no
    # character, because a modifier produces none.
    modifier = dut.expect(
        re.compile(rb"KEY usage=(\d+) ascii=(\d+) pressed=1 released=0 "
                   rb"mods=0x(\w+) caps=\d+ length=\d+ context=loop"),
        timeout=25,
    )
    assert int(modifier.group(1)) == 0xe1, "0xe1 is Left Shift"
    assert modifier.group(2) == b"0", "a modifier produces no character"

    # Two keys at once: the state has both, and only the newly pressed one is an
    # event — 0x04 was already down, so it did not change.
    device.write("b")
    device.expect_exact("DEVICE_SEND two=1 error=NONE", timeout=20)
    both = dut.expect(
        re.compile(rb"STATE mods=0x(\w+) down=([\w,]+) count=(\d+)"), timeout=25
    )
    assert {int(value) for value in both.group(2).decode().split(",")} == \
        {0x04, 0x05, 0xe1}, "both keys and the modifier are down: %s" % \
        both.group(2).decode()
    second = dut.expect(
        re.compile(rb"KEY usage=(\d+) ascii=(\d+) pressed=1 released=0 "
                   rb"mods=0x(\w+) caps=\d+ length=\d+ context=loop"),
        timeout=25,
    )
    assert int(second.group(1)) == 0x05, (
        "only the newly pressed key is an event; 0x04 was already down"
    )

    device.write("r")
    device.expect_exact("DEVICE_SEND release=1 error=NONE", timeout=20)
    released = dut.expect(
        re.compile(rb"STATE mods=0x(\w+) down=(\w+) count=(\d+)"), timeout=25
    )
    assert released.group(2) == b"none" and released.group(3) == b"0", (
        "releaseAll() must leave nothing down: %s" % released.group(2).decode()
    )

    # The one report a keyboard host writes. The device receives it as its LED state.
    dut.write("l")
    dut.expect_exact("LEDS accepted=1 error=NONE", timeout=20)
    device.expect_exact("DEVICE_LED leds=0x02 caps=1 context=loop", timeout=25)
    device.write("?")
    device.expect_exact("DEVICE_STATE started=1 ready=1 caps=1", timeout=25)
    dut.write("L")
    dut.expect_exact("LEDS accepted=1 error=NONE", timeout=20)
    device.expect_exact("DEVICE_LED leds=0x00 caps=0 context=loop", timeout=25)

    # A disconnect takes the discovered handles with it: ready() must not survive.
    dut.write("x")
    dut.expect_exact("DISCONNECT accepted=1", timeout=20)
    dut.expect(re.compile(rb"DISCONNECTED id=%d" % connection_id), timeout=25)
    dut.write("?")
    dut.expect_exact("READY_STATE id=0 ready=0 invalid=0", timeout=25)
