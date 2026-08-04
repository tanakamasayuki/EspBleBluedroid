import re


def test_the_midi_device_puts_specification_bytes_on_the_air(dut, peers):
    """BLE MIDI Device: the packets the helper sends and the ones it parses.

    `EspBleMidiDevice` is EspBle's file with the library reference retyped, and
    `EspBleMidi.h` is byte-identical, so what needs checking on hardware is not
    the API but the wire: the header/timestamp encoding, running status carried
    across a packet, and a SysEx split across several notifications.

    The peer is a raw Arduino-ESP32 central that decodes the BLE MIDI header with
    its own arithmetic and builds the packets it writes by hand, so both
    directions are compared against the specification rather than against the
    same codec twice.
    """
    peer = peers["device"]
    dut.write("?")
    dut.expect_exact("READY_STATE started=1 ready=0", timeout=30)
    peer.expect_exact("MIDI_DEVICE_PEER_READY", timeout=30)

    peer.write("c")
    peer.expect_exact("PEER_CONNECTED characteristic=1", timeout=40)
    peer.write("s")
    peer.expect_exact("PEER_SUBSCRIBED", timeout=25)
    # ready() follows the CCCD, so it is only true once the peer has subscribed.
    dut.write("?")
    dut.expect_exact("READY_STATE started=1 ready=1", timeout=20)

    # Device -> host. The timestamp moves, so the assertion is the MIDI bytes the
    # peer recovered after stripping the two header bytes it validated.
    for command, midi in (
        ("n", "903c64"),   # Note On, channel 0, middle C, velocity 100
        ("f", "803c00"),   # Note Off
        ("c", "b30764"),   # Control Change 7 on channel 3
        ("p", "e00040"),   # Pitch Bend centre, split into two 7-bit halves
        ("g", "c92a"),     # Program Change 42 on channel 9 (one data byte)
    ):
        dut.write(command)
        dut.expect_exact("SEND accepted=1", timeout=20)
        peer.expect(
            re.compile(
                rb"PEER_NOTIFY length=%d ts=\d+ midi=%s"
                % (len(midi) // 2 + 2, midi.encode())
            ),
            timeout=25,
        )

    # A 99-byte SysEx does not fit one 23-byte-MTU notification, so the helper
    # splits it and sends one packet per completion. Nothing else may be sent
    # while that runs.
    dut.write("s")
    dut.expect_exact(
        "SEND_SYSEX accepted=1 length=99 second=0 note=0 sending=1", timeout=20
    )
    dut.expect_exact("SYSEX_SENT", timeout=40)
    peer.write("q")
    report = peer.expect(
        re.compile(rb"PEER_REPORT notifications=(\d+) header_failures=0"), timeout=20
    )
    # 97 payload bytes: the 0x7D identifier and a 96-byte ramp, with the 0xF0/0xF7
    # framing stripped. `ramp=1` is what proves the packets arrived in order and
    # none was dropped.
    sysex = peer.expect(
        re.compile(rb"PEER_SYSEX packets=(\d+) bytes=97 start=1 end=1 ramp=1"),
        timeout=20,
    )
    packets = int(sysex.group(1))
    assert packets >= 4, (
        "97 payload bytes at a 23-byte MTU have to span several packets"
    )
    # Every notification is accounted for: the five messages above plus the SysEx
    # packets, and nothing extra went out while the transfer was running.
    assert int(report.group(1)) == 5 + packets, (
        "unexpected notifications on the air: %s" % report.group(1)
    )

    # Host -> device: hand-built packets, including running status.
    peer.write("n")
    peer.expect_exact("PEER_WROTE note_on length=5", timeout=20)
    dut.expect(
        re.compile(
            rb"MIDI_IN status=0x90 data1=60 data2=100 length=2 ts=\d+ context=loop"
        ),
        timeout=25,
    )
    peer.write("r")
    peer.expect_exact("PEER_WROTE running_status length=7", timeout=20)
    dut.expect(
        re.compile(rb"MIDI_IN status=0x90 data1=60 data2=100 length=2"), timeout=25
    )
    # The second message carries no status byte of its own; the parser has to
    # carry 0x90 over.
    dut.expect(
        re.compile(rb"MIDI_IN status=0x90 data1=62 data2=101 length=2"), timeout=25
    )

    # A SysEx written across three packets: the middle one has no timestamp and
    # the last one has one before 0xF7. No write is longer than 20 bytes, because
    # nothing raised the MTU on this link and a longer ATT write would be
    # truncated on the peer before reaching the air.
    peer.write("y")
    peer.expect_exact("PEER_WROTE sysex_first length=14", timeout=20)
    dut.expect(re.compile(rb"MIDI_IN_SYSEX start=1 end=0 length=11"), timeout=25)
    peer.expect_exact("PEER_WROTE sysex_continuation length=20", timeout=20)
    dut.expect(re.compile(rb"MIDI_IN_SYSEX start=0 end=0 length=19"), timeout=25)
    peer.expect_exact("PEER_WROTE sysex_last length=13", timeout=20)
    dut.expect(re.compile(rb"MIDI_IN_SYSEX start=0 end=0 length=10"), timeout=25)
    # The 0xF7 in the last packet is preceded by a timestamp, so the end of the
    # stream is its own chunk carrying no payload.
    dut.expect(re.compile(rb"MIDI_IN_SYSEX start=0 end=1 length=0"), timeout=25)

    dut.write("r")
    dut.expect_exact(
        "RECEIVED messages=3 status=0x90 data1=62 data2=101", timeout=20
    )
    # 40 bytes: the 0x7D identifier and a 39-byte ramp reassembled from three
    # packets, in order.
    dut.expect(
        re.compile(rb"RECEIVED_SYSEX chunks=4 bytes=40 start=1 end=1 ramp=1"),
        timeout=20,
    )

    peer.write("x")
    peer.expect_exact("PEER_DISCONNECT_REQUESTED", timeout=20)
