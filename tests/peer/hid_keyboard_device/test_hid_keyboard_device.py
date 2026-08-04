import re

# The 6KRO keyboard Report Descriptor, byte for byte. `tests/unit/hid_report_maps`
# pins the same bytes against EspBle's table on the host side; this is the
# assertion that they reach the air, which is the only thing a host OS acts on.
KEYBOARD_REPORT_MAP = (
    "05010906a1018501050719e029e71500250175019508810295017508810195067508"
    "150025650507190029658100950575010508190129059102950175039101c0"
)
# Shift + 'a' as the 8-byte Report-protocol keyboard report:
# [modifiers, reserved, keycode1..6].
SHIFT_A_REPORT = "0200040000000000"
RELEASED_REPORT = "0000000000000000"


def test_a_hid_keyboard_is_discoverable_and_usable_by_a_host(dut, peers):
    """HID over GATT keyboard: what a host OS can read, write and receive.

    The descriptor bytes are already pinned without hardware, but a table proves
    nothing about the published database. Here a raw Arduino-ESP32 central walks
    the service the way a host does: the Report Map read (long, at the default
    MTU), the HID Information, the two 0x2A4D Report characteristics told apart by
    their Report Reference descriptors — the duplicate-UUID shape HOGP needs — the
    input report notification, the LED output write coming back through
    `onOutputReport()` and `ledState()`, and the Protocol Mode write.

    Security is off in this scenario so the instrument can stay a plain central;
    the encrypted-attribute half of HOGP belongs with the security suites.
    """
    peer = peers["device"]
    dut.write("?")
    dut.expect_exact(
        "READY_STATE started=1 configured=1 ready=0", timeout=30
    )
    peer.expect_exact("HID_KEYBOARD_PEER_READY", timeout=30)

    # A send before any host is there fails with the state that says so, rather
    # than pretending to have delivered a keystroke.
    dut.write("k")
    dut.expect_exact("SEND accepted=0 error=INVALID_STATE", timeout=20)

    peer.write("c")
    peer.expect_exact("PEER_CONNECTED hid=1", timeout=40)

    # What a host reads first: the descriptor and the HID Information.
    peer.write("m")
    peer.expect_exact(
        "PEER_REPORT_MAP length=%d hex=%s"
        % (len(KEYBOARD_REPORT_MAP) // 2, KEYBOARD_REPORT_MAP),
        timeout=30,
    )
    # HID 1.11, country code 0, remote wake.
    peer.write("i")
    peer.expect_exact("PEER_HID_INFORMATION hex=11010001", timeout=25)

    # The two Report characteristics share UUID 0x2A4D and are only distinguishable
    # by their Report Reference descriptor: report 1 Input, report 1 Output.
    peer.write("d")
    peer.expect_exact(
        "PEER_REPORTS matches=2 input=1 input_ref=0101 output=1 output_ref=0102 "
        "distinct=1",
        timeout=30,
    )

    # Device Information carries what configure() was given.
    peer.write("I")
    peer.expect_exact(
        "PEER_PNP_ID hex=023412cdab0201 manufacturer=EspBleBluedroid", timeout=25
    )
    peer.write("B")
    peer.expect_exact("PEER_BATTERY level=77", timeout=25)

    # ready() follows the input report's CCCD, which is what a host writes when it
    # is prepared to receive keystrokes.
    peer.write("s")
    peer.expect_exact("PEER_SUBSCRIBED", timeout=30)
    dut.write("?")
    dut.expect_exact("READY_STATE started=1 configured=1 ready=1", timeout=25)

    # An explicit report, so the wire bytes are the assertion.
    dut.write("k")
    dut.expect_exact("SEND accepted=1 error=NONE", timeout=20)
    peer.expect(
        re.compile(rb"PEER_INPUT handle=\d+ length=8 hex=%s" % SHIFT_A_REPORT.encode()),
        timeout=25,
    )
    dut.write("r")
    dut.expect_exact("RELEASE accepted=1", timeout=20)
    peer.expect(
        re.compile(rb"PEER_INPUT handle=\d+ length=8 hex=%s" % RELEASED_REPORT.encode()),
        timeout=25,
    )

    # The layout path: pressKey('A') has to find Shift + usage 0x04 in the keymap
    # tables, which is the same report the explicit call produced.
    dut.write("w")
    dut.expect_exact("WRITE accepted=1 error=NONE", timeout=20)
    peer.expect(
        re.compile(rb"PEER_INPUT handle=\d+ length=8 hex=%s" % SHIFT_A_REPORT.encode()),
        timeout=25,
    )
    dut.write("r")
    dut.expect_exact("RELEASE accepted=1", timeout=20)

    # A host writing the LED output report: delivered from update(), and reflected
    # in ledState() for callers that ask rather than react.
    peer.write("o")
    peer.expect_exact("PEER_OUTPUT_WRITTEN", timeout=25)
    dut.expect(
        re.compile(
            rb"OUTPUT_REPORT id=\d+ leds=0x02 num=0 caps=1 scroll=0 context=loop"
        ),
        timeout=25,
    )
    dut.write("l")
    dut.expect(re.compile(rb"LED_STATE id=\d+ leds=0x02 num=0 caps=1"), timeout=20)

    # Protocol Mode: the host selects Boot Protocol. This device did not enable the
    # boot characteristics, so the mode is recorded and reported while reports keep
    # going out over the Report-protocol characteristic.
    dut.write("m")
    dut.expect_exact("MODE mode=1", timeout=20)
    peer.write("p")
    peer.expect_exact("PEER_MODE_WRITTEN before=1", timeout=25)
    dut.expect(re.compile(rb"PROTOCOL_MODE mode=0 id=\d+ context=loop"), timeout=25)
    dut.write("m")
    dut.expect_exact("MODE mode=0", timeout=20)
    dut.write("k")
    dut.expect_exact("SEND accepted=1 error=NONE", timeout=20)
    peer.expect(
        re.compile(rb"PEER_INPUT handle=\d+ length=8 hex=%s" % SHIFT_A_REPORT.encode()),
        timeout=25,
    )

    # Battery notifications go to a host that subscribed; the value is readable
    # either way.
    dut.write("b")
    dut.expect_exact("BATTERY accepted=1", timeout=20)
    peer.write("B")
    peer.expect_exact("PEER_BATTERY level=42", timeout=25)

    # NKRO after configure() is refused: the descriptor is already published and it
    # decides the report layout.
    dut.write("n")
    dut.expect_exact("NKRO enabled=0", timeout=20)

    peer.write("x")
    peer.expect_exact("PEER_DISCONNECT_REQUESTED", timeout=20)
    # The host is gone, so nothing is ready and the LED state is not a previous
    # host's.
    dut.write("?")
    dut.expect_exact("READY_STATE started=1 configured=1 ready=0", timeout=25)
    dut.write("l")
    dut.expect_exact("LED_STATE id=0 leds=0x00 num=0 caps=0", timeout=20)
