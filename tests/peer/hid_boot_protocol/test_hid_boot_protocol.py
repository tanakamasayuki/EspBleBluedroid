import re

# The usages the sketch sends, and what each one becomes in each protocol mode.
THREE_KEYS = (0x04, 0x05, 0x1e)
SEVEN_KEYS = (0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a)
NKRO_LENGTH = 29
BOOT_LENGTH = 8


def nkro_report(usages):
    """The 29-byte wire report: a modifier byte then a 224-bit usage bitmap."""
    report = bytearray(NKRO_LENGTH)
    for usage in usages:
        report[1 + (usage >> 3)] |= 1 << (usage & 7)
    return bytes(report).hex()


def boot_report(usages):
    """The fixed boot layout: [modifiers, reserved, keycode1..6].

    Six is all it holds, so more than six held keys is not a truncation but the HID
    rollover code 0x01 in every slot — the host is told "too many", not a subset.
    """
    if len(usages) > 6:
        return bytes([0, 0] + [0x01] * 6).hex()
    keys = sorted(usages)
    return bytes([0, 0] + keys + [0] * (6 - len(keys))).hex()


def test_boot_protocol_mode_switches_the_report(dut, peers):
    """The same keystroke over the report the Host's Protocol Mode selects.

    Boot Protocol exists for hosts that cannot parse a Report Descriptor, so the
    device has to answer with the fixed 8-byte layout no matter what its Report Map
    declares. This keyboard is NKRO, so the two are as far apart as they get: 29
    bytes of bitmap in Report Protocol Mode, and the same usages as keycodes in Boot
    Protocol Mode — with the rollover code when more than six are held. What is
    checked is the switch itself (the reports arrive on different handles), the
    conversion, and that `ready()` follows the CCCD of whichever report is live.
    """
    peer = peers["device"]
    dut.write("?")
    dut.expect_exact(
        "READY_STATE started=1 nkro=1 mode=1 ready=0 mtu=23", timeout=30
    )
    peer.expect_exact("HID_BOOT_PROTOCOL_PEER_READY", timeout=30)

    peer.write("c")
    peer.expect_exact("PEER_CONNECTED hid=1", timeout=40)

    # Boot Protocol adds two characteristics with UUIDs of their own — that is how a
    # host that cannot read a Report Map finds the keyboard reports at all.
    peer.write("d")
    discovered = peer.expect(
        re.compile(rb"PEER_DISCOVERED report=(\d+) boot_in=(\d+) boot_out=(\d+) "
                   rb"mode=(\d+)"),
        timeout=30,
    )
    report_handle = int(discovered.group(1))
    boot_handle = int(discovered.group(2))
    handles = [int(discovered.group(index)) for index in range(1, 5)]
    assert all(handle != 0 for handle in handles), (
        "Report input, Boot Keyboard Input/Output and Protocol Mode must all exist: "
        "%s" % handles
    )
    assert len(set(handles)) == 4, "each is its own attribute: %s" % handles

    # Report Protocol Mode is the default; a host only leaves it deliberately.
    peer.write("p")
    peer.expect_exact("PEER_MODE_READ length=1 value=1", timeout=25)

    # Subscribe to both Input Reports, so which one carries a keystroke is the
    # device's decision rather than the only subscription available.
    peer.write("s")
    peer.expect_exact("PEER_REPORT_SUBSCRIBED", timeout=30)
    peer.write("S")
    peer.expect_exact("PEER_BOOT_SUBSCRIBED", timeout=30)

    # In Report Protocol Mode the NKRO report goes out as the Report Map declares it.
    dut.write("?")
    state = dut.expect(
        re.compile(rb"READY_STATE started=1 nkro=1 mode=1 ready=1 mtu=(\d+)"),
        timeout=25,
    )
    assert int(state.group(1)) >= NKRO_LENGTH + 3, (
        "a %d-byte report needs an ATT payload that large: mtu=%s"
        % (NKRO_LENGTH, state.group(1).decode())
    )
    dut.write("3")
    dut.expect_exact("SEND three=1 error=NONE", timeout=20)
    peer.expect_exact(
        "PEER_INPUT handle=%d length=%d hex=%s"
        % (report_handle, NKRO_LENGTH, nkro_report(THREE_KEYS)),
        timeout=25,
    )

    # The Host selects Boot Protocol Mode by writing the Protocol Mode
    # characteristic. The device observes it from update(), with the connection.
    peer.write("b")
    peer.expect_exact("PEER_MODE_WRITTEN mode=0", timeout=20)
    mode = dut.expect(
        re.compile(rb"PROTOCOL_MODE mode=0 id=(\d+) context=(\w+)"), timeout=25
    )
    assert int(mode.group(1)) != 0, "the event must name the connection"
    assert mode.group(2) == b"loop", "the callback must run in the caller's update()"
    dut.write("?")
    dut.expect_exact(
        "READY_STATE started=1 nkro=1 mode=0 ready=1 mtu=%s"
        % state.group(1).decode(),
        timeout=25,
    )

    # The same usages, now on the Boot Keyboard Input Report and in its layout. The
    # application sent an NKRO report either way; the conversion is the library's.
    dut.write("3")
    dut.expect_exact("SEND three=1 error=NONE", timeout=20)
    peer.expect_exact(
        "PEER_INPUT handle=%d length=%d hex=%s"
        % (boot_handle, BOOT_LENGTH, boot_report(THREE_KEYS)),
        timeout=25,
    )

    # Seven held keys do not fit six slots, so the host is told "too many" with the
    # HID rollover code rather than given an arbitrary subset.
    dut.write("7")
    dut.expect_exact("SEND seven=1 error=NONE", timeout=20)
    peer.expect_exact(
        "PEER_INPUT handle=%d length=%d hex=%s"
        % (boot_handle, BOOT_LENGTH, boot_report(SEVEN_KEYS)),
        timeout=25,
    )
    dut.write("r")
    dut.expect_exact("SEND release=1 error=NONE", timeout=20)
    peer.expect_exact(
        "PEER_INPUT handle=%d length=%d hex=%s"
        % (boot_handle, BOOT_LENGTH, boot_report(())),
        timeout=25,
    )

    # The LED report a boot host writes goes to the Boot Keyboard Output Report, and
    # has to reach the same callback as the Report-protocol one.
    peer.write("o")
    peer.expect_exact("PEER_BOOT_OUTPUT_WRITTEN", timeout=25)
    dut.expect_exact("LED_STATE leds=0x02 caps=1 context=loop", timeout=25)

    # ready() follows the report the selected mode actually uses: with the Boot
    # Keyboard CCCD off, a Boot-Protocol host is not listening — even though the
    # Report-protocol subscription is still in place.
    peer.write("u")
    peer.expect_exact("PEER_BOOT_UNSUBSCRIBED", timeout=25)
    dut.write("?")
    dut.expect_exact(
        "READY_STATE started=1 nkro=1 mode=0 ready=0 mtu=%s"
        % state.group(1).decode(),
        timeout=25,
    )
    dut.write("3")
    dut.expect_exact("SEND three=0 error=INVALID_STATE", timeout=20)

    # Back to Report Protocol Mode: the keystroke returns to the other handle in the
    # other layout, and the Boot subscription no longer matters.
    peer.write("B")
    peer.expect_exact("PEER_MODE_WRITTEN mode=1", timeout=20)
    dut.expect(re.compile(rb"PROTOCOL_MODE mode=1 id=(\d+) context=loop"), timeout=25)
    dut.write("3")
    dut.expect_exact("SEND three=1 error=NONE", timeout=20)
    peer.expect_exact(
        "PEER_INPUT handle=%d length=%d hex=%s"
        % (report_handle, NKRO_LENGTH, nkro_report(THREE_KEYS)),
        timeout=25,
    )

    peer.write("x")
    peer.expect_exact("PEER_DISCONNECT_REQUESTED", timeout=20)
