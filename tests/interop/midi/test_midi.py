import re

# The message bytes both sides send, in the order the tests send them. Sharing the
# expectations is the rule in tests/TEST_PLAN.md: the same hex on both stacks.
MESSAGES = (
    ("n", "0x90", 60, 100, 2),   # Note On, channel 0, middle C, velocity 100
    ("o", "0xb3", 7, 100, 2),    # Control Change 7 on channel 3
    ("g", "0xc9", 42, 0, 1),     # Program Change 42 on channel 9
)
# 97 payload bytes: the 0x7D identifier and a 96-byte ramp, with the 0xF0/0xF7
# framing stripped. `ramp=1` is what proves the packets arrived in order and none
# was dropped.
SYSEX_PAYLOAD = 97


def expect_messages(sender, receiver, send_prefix, receive_prefix, writes=False):
    """Send each message with one library and decode it with the other.

    `writes` waits for this library's write completion between sends: a Bluedroid
    central runs one GATT operation at a time, so issuing the next message before
    the previous write has completed would be refused with `InvalidState`. The
    receiving side's log line does not prove the sender is done with the operation,
    which is why the sender's own completion is what the test waits for.
    """
    for command, status, data1, data2, length in MESSAGES:
        sender.write(command)
        sender.expect_exact("%s_SEND accepted=1" % send_prefix, timeout=25)
        receiver.expect(
            re.compile(
                rb"%s_MIDI_IN status=%s data1=%d data2=%d length=%d"
                % (receive_prefix.encode(), status.encode(), data1, data2, length)
            ),
            timeout=30,
        )
        if writes:
            sender.expect(
                re.compile(rb"BLUEDROID_WRITE_DONE success=1 context=loop"),
                timeout=25,
            )


def expect_sysex(sender, receiver, send_prefix, receive_prefix):
    """A 99-byte SysEx crosses the stacks whole, and in order."""
    sender.write("s")
    sender.expect_exact("%s_SEND_SYSEX accepted=1 length=99" % send_prefix, timeout=25)
    sender.expect_exact("%s_SYSEX_SENT" % send_prefix, timeout=60)
    # The sender being done does not mean the receiver has parsed it, so the end of
    # the stream on the receiving side is what the report is read after. The 0xF7 is
    # preceded by a timestamp, so the end arrives as its own chunk with no payload.
    receiver.expect(
        re.compile(rb"%s_MIDI_IN_SYSEX .*end=1" % receive_prefix.encode()),
        timeout=40,
    )
    receiver.write("r")
    receiver.expect(
        re.compile(
            rb"%s_RECEIVED_SYSEX chunks=\d+ bytes=%d start=1 end=1 ramp=1"
            % (receive_prefix.encode(), SYSEX_PAYLOAD)
        ),
        timeout=25,
    )


def test_bluedroid_midi_host_against_an_espble_midi_device(dut, peers):
    """BLE MIDI across the stacks: EspBle is the instrument, this library the host.

    The codec header is byte-identical in both libraries and the profile helper
    differs only in the type of the library reference, and both of those are
    checked without hardware (`tests/unit/midi`, `tests/unit/api_parity`). What
    only two stacks on the air can show is whether the transport agrees: the CCCD
    write, notifications against a negotiated MTU, Write Without Response, and a
    SysEx spanning packets. Each side encodes and decodes with its own library, so
    a shared assumption cannot cancel itself out.

    The BLE MIDI service and characteristic UUIDs are fixed by the specification,
    so this scenario cannot isolate itself with a suite-tag UUID the way the others
    do; the role is part of the advertised name instead, and each side requires both
    the name and the service UUID.
    """
    peer = peers["device"]
    # Requested rather than waited for: the boot banner of whichever board is
    # flashed first is lost while the other one is flashed.
    dut.write("?")
    dut.expect_exact("ESPBLE_STATE mode=- started=0 ready=0", timeout=30)
    peer.write("?")
    peer.expect_exact("BLUEDROID_STATE mode=- started=0 ready=0", timeout=30)

    dut.write("d")
    dut.expect_exact("ESPBLE_MODE_STARTED mode=d success=1 error=NONE", timeout=30)
    peer.write("h")
    peer.expect_exact(
        "BLUEDROID_MODE_STARTED mode=h success=1 error=NONE", timeout=30
    )
    peer.write("c")
    peer.expect_exact("BLUEDROID_SCAN_STARTED 1", timeout=25)
    peer.expect_exact("BLUEDROID_TARGET_FOUND connect=1", timeout=40)
    # discover() is issued from onConnected and subscribes once the characteristic
    # is found; ready() follows the CCCD, so it is the proof the subscription
    # crossed the stacks.
    peer.expect(
        re.compile(rb"BLUEDROID_CONNECTED id=\d+ discover=1 context=loop"),
        timeout=40,
    )
    # Discovery and the CCCD write are a round trip each, so they are waited for
    # rather than polled -- and a failure here names the step that failed.
    peer.expect_exact("BLUEDROID_DISCOVERED success=1 error=NONE context=loop", timeout=40)
    peer.expect_exact("BLUEDROID_SUBSCRIBED success=1 error=NONE context=loop", timeout=40)
    dut.expect_exact("ESPBLE_SUBSCRIPTION notifications=1", timeout=40)
    peer.write("?")
    peer.expect(re.compile(rb"BLUEDROID_STATE mode=h started=1 ready=1"), timeout=30)
    dut.write("?")
    dut.expect_exact("ESPBLE_STATE mode=d started=1 ready=1", timeout=25)

    # EspBle notifies, this library decodes.
    expect_messages(dut, peer, "ESPBLE", "BLUEDROID")
    expect_sysex(dut, peer, "ESPBLE", "BLUEDROID")
    peer.write("r")
    peer.expect_exact(
        "BLUEDROID_RECEIVED messages=3 status=0xc9 data1=42 data2=0", timeout=25
    )

    # This library writes, EspBle decodes.
    expect_messages(peer, dut, "BLUEDROID", "ESPBLE", writes=True)
    expect_sysex(peer, dut, "BLUEDROID", "ESPBLE")
    dut.write("r")
    dut.expect_exact(
        "ESPBLE_RECEIVED messages=3 status=0xc9 data1=42 data2=0", timeout=25
    )


def test_espble_midi_host_against_a_bluedroid_midi_device(dut, peers):
    """The same exchange with the roles swapped, which is a different code path.

    Notifying as a peripheral and writing as a central are not the two halves of
    one mechanism, so agreement in one direction says nothing about the other. Here
    this library is the instrument and EspBle the host, so the notification path
    under test is Bluedroid's GATT Server and the write path is NimBLE's client.
    """
    peer = peers["device"]

    peer.write("d")
    peer.expect_exact(
        "BLUEDROID_MODE_STARTED mode=d success=1 error=NONE", timeout=30
    )
    dut.write("h")
    dut.expect_exact("ESPBLE_MODE_STARTED mode=h success=1 error=NONE", timeout=30)
    dut.write("c")
    dut.expect_exact("ESPBLE_SCAN_STARTED 1", timeout=25)
    dut.expect_exact("ESPBLE_TARGET_FOUND connect=1", timeout=40)
    dut.expect(re.compile(rb"ESPBLE_CONNECTED id=\d+ discover=1"), timeout=40)
    dut.expect_exact("ESPBLE_DISCOVERED success=1 error=NONE", timeout=40)
    dut.expect_exact("ESPBLE_SUBSCRIBED success=1 error=NONE", timeout=40)
    peer.expect_exact(
        "BLUEDROID_SUBSCRIPTION notifications=1 context=loop", timeout=40
    )
    dut.write("?")
    dut.expect(re.compile(rb"ESPBLE_STATE mode=h started=1 ready=1"), timeout=30)
    peer.write("?")
    peer.expect_exact("BLUEDROID_STATE mode=d started=1 ready=1", timeout=25)

    # This library notifies as the peripheral, EspBle decodes.
    expect_messages(peer, dut, "BLUEDROID", "ESPBLE")
    expect_sysex(peer, dut, "BLUEDROID", "ESPBLE")
    dut.write("r")
    dut.expect_exact(
        "ESPBLE_RECEIVED messages=3 status=0xc9 data1=42 data2=0", timeout=25
    )

    # EspBle writes as the central, this library decodes — and reports the callback
    # context, because dispatch from `update()` is part of its contract.
    expect_messages(dut, peer, "ESPBLE", "BLUEDROID")
    expect_sysex(dut, peer, "ESPBLE", "BLUEDROID")
    peer.write("r")
    peer.expect_exact(
        "BLUEDROID_RECEIVED messages=3 status=0xc9 data1=42 data2=0", timeout=25
    )
