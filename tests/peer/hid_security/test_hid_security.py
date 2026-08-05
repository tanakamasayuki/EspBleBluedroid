import re

# The Report Map of the fixed keyboard profile. Its exact bytes are pinned by
# tests/unit/hid_report_maps and by peer/hid_keyboard_device; what matters here is
# only that a length appears at all, and that it does not before pairing.
MINIMUM_REPORT_MAP_LENGTH = 60


def read_attribute(peer, label, timeout=25):
    """Return the length the raw central got back, or None when the attribute was
    not even in its map."""
    match = peer.expect(
        re.compile(
            rb"PEER_" + label.encode() + rb"(?: missing=(1)| length=(\d+) hex=(\w*))"
        ),
        timeout=timeout,
    )
    if match.group(1):
        return None
    return int(match.group(2))


def test_hid_attributes_need_encryption(dut, peers):
    """HOGP attributes answer nothing until the link is encrypted.

    HID over GATT requires Security Mode 1 Level 2 on the HID service.
    peer/hid_keyboard_device runs the same device with security off so its instrument
    can stay a plain central; this is the half that shows the attributes are
    protected at all.

    Getting to the unpaired case took a correction. Connecting without configuring
    any security and reading is not enough: **this device asks for security as soon
    as a host connects** (the peripheral-initiated Security Request a HOGP device may
    send), and the wrapper's default is to accept, so the link was already encrypted
    before the first read. The real unpaired case is a host whose user dismisses the
    pairing dialog, which is what `onSecurityRequest()` returning false is. Both
    phases are asserted here: the refusal leaves every HID attribute mute, and
    accepting makes the same attributes readable.

    Both boards clear their bonds first: a bond left over from an earlier run would
    encrypt the link immediately and the unpaired phase would prove nothing.
    """
    peer = peers["device"]
    dut.expect_exact("HID_SECURITY_READY", timeout=30)
    peer.expect_exact("HID_SECURITY_PEER_READY", timeout=30)

    dut.write("c")
    dut.expect_exact("BONDS_CLEARED success=1 count=0", timeout=25)
    peer.write("C")
    peer.expect_exact("PEER_BONDS_CLEARED success=1 count=0", timeout=25)
    dut.write("?")
    dut.expect_exact("STATE started=1 ready=0 secured=0 bonds=0", timeout=25)

    # Phase 1: the host refuses to pair.
    peer.write("R")
    peer.expect_exact("PEER_PAIRING_MODE accept=0", timeout=20)
    peer.write("c")
    # The request arrives while connect() is still running — the device asks as soon
    # as the link is up, before the client has finished discovering services — so it
    # has to be read before PEER_CONNECTED or it is skipped past and lost.
    peer.expect_exact("PEER_SECURITY_REQUEST accepted=0", timeout=40)
    peer.expect_exact("PEER_SECURITY success=0 bonded=0", timeout=25)
    connected = peer.expect(
        re.compile(rb"PEER_CONNECTED hid=(\d+) secured=(\d+)"), timeout=40
    )
    assert connected.group(1) == b"1", (
        "the HID service must be discoverable without encryption; only its values "
        "are protected"
    )
    assert connected.group(2) == b"0", "the host refused, so nothing is encrypted"
    refused = dut.expect(
        re.compile(rb"SECURITY success=(\d+) encrypted=(\d+) bonded=(\d+) "
                   rb"key=(\d+) context=(\w+)"),
        timeout=30,
    )
    assert refused.group(1) == b"0", "a refused pairing must be reported as a failure"
    assert refused.group(2) == b"0", "and must not leave the link encrypted"
    assert refused.group(5) == b"loop", "events must be delivered from update()"

    # Still connected, so a mute attribute below is the protection and not a dropped
    # link.
    peer.write("?")
    peer.expect_exact("PEER_STATE connected=1 secured=0 bonds=0", timeout=25)

    peer.write("m")
    unencrypted_map = read_attribute(peer, "REPORT_MAP")
    assert unencrypted_map == 0, (
        "the Report Map answered %s bytes on an unencrypted link; a host would have "
        "the whole report layout without pairing" % unencrypted_map
    )
    peer.write("i")
    unencrypted_information = read_attribute(peer, "HID_INFORMATION")
    assert unencrypted_information == 0, (
        "HID Information answered %s bytes unencrypted" % unencrypted_information
    )
    # The Report Reference descriptors are protected too, not only the
    # characteristic values: they are what names each 0x2A4D attribute.
    peer.write("f")
    references = peer.expect(
        re.compile(rb"PEER_REFERENCES reports=(\d+) readable=(\d+)"), timeout=30
    )
    assert int(references.group(1)) >= 2, (
        "the keyboard publishes an Input and an Output Report characteristic"
    )
    assert references.group(2) == b"0", (
        "%s Report Reference descriptors were readable unencrypted"
        % references.group(2).decode()
    )
    dut.write("?")
    dut.expect_exact("STATE started=1 ready=0 secured=0 bonds=0", timeout=25)

    # Phase 2: the same reads once the host accepts.
    peer.write("x")
    peer.expect_exact("PEER_DISCONNECTED", timeout=25)
    dut.write("A")
    dut.expect_exact("ADVERTISE restarted=1", timeout=25)
    peer.write("S")
    peer.expect_exact("PEER_PAIRING_MODE accept=1", timeout=20)

    peer.write("c")
    peer.expect_exact("PEER_SECURITY_REQUEST accepted=1", timeout=40)
    peer.expect_exact("PEER_SECURITY success=1 bonded=1", timeout=30)
    peer.expect(re.compile(rb"PEER_CONNECTED hid=1 secured=1"), timeout=40)
    security = dut.expect(
        re.compile(rb"SECURITY success=(\d+) encrypted=(\d+) bonded=(\d+) "
                   rb"key=(\d+) context=(\w+)"),
        timeout=40,
    )
    assert security.group(1) == b"1", "pairing failed on the device side"
    assert security.group(2) == b"1", "the link must be encrypted"
    assert security.group(3) == b"1", "bonding was requested by both sides"
    assert int(security.group(4)) == 16, "the key size is 16 bytes"

    peer.write("m")
    encrypted_map = read_attribute(peer, "REPORT_MAP", timeout=30)
    assert encrypted_map is not None and encrypted_map >= MINIMUM_REPORT_MAP_LENGTH, (
        "the Report Map must be readable once encrypted, and it is longer than one "
        "ATT payload: got %s bytes" % encrypted_map
    )
    peer.write("i")
    information = read_attribute(peer, "HID_INFORMATION", timeout=30)
    assert information == 4, "HID Information is 4 bytes"
    peer.write("f")
    readable = peer.expect(
        re.compile(rb"PEER_REFERENCES reports=(\d+) readable=(\d+)"), timeout=30
    )
    assert readable.group(1) == readable.group(2), (
        "every Report Reference is readable once encrypted: %s of %s"
        % (readable.group(2).decode(), readable.group(1).decode())
    )

    # The bond is on both sides, which is what makes the next connection encrypt
    # without pairing again.
    dut.write("?")
    dut.expect_exact("STATE started=1 ready=0 secured=1 bonds=1", timeout=25)
    peer.write("?")
    peer.expect_exact("PEER_STATE connected=1 secured=1 bonds=1", timeout=25)
