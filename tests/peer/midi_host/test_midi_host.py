import re


def test_the_midi_host_decodes_specification_bytes(dut, peers):
    """BLE MIDI Host: what the parser makes of hand-built packets, and what it writes.

    The peer is a raw Arduino-ESP32 BLE MIDI peripheral whose notifications are
    assembled byte by byte from the wire format — running status carried across a
    packet, a System Real-Time byte between two messages, a SysEx over three
    notifications — so the decode is checked against the specification rather than
    against the same codec on both ends. The peer decodes what the host writes the
    same way, which pins the encoder.

    A central here runs one GATT operation at a time, so the test waits for each
    write completion before issuing the next send; that completion is also what
    drives the helper's own SysEx packets.
    """
    peer = peers["device"]
    dut.write("?")
    dut.expect_exact("READY_STATE started=1 id=0 ready=0", timeout=30)
    peer.expect_exact("MIDI_HOST_PEER_READY", timeout=30)

    dut.write("c")
    dut.expect_exact("SCAN_STARTED 1", timeout=20)
    dut.expect_exact("TARGET_FOUND connect=1", timeout=40)
    # discover() is issued from onConnected and subscribes once the characteristic
    # is found.
    dut.expect(re.compile(rb"CONNECTED id=\d+ discover=1 context=loop"), timeout=40)
    peer.expect_exact("PEER_CONNECTED", timeout=30)
    peer.expect_exact("PEER_SUBSCRIPTION notifications=1", timeout=40)
    dut.write("?")
    ready = dut.expect(
        re.compile(rb"READY_STATE started=1 id=(\d+) ready=1"), timeout=25
    )
    connection_id = int(ready.group(1))
    assert connection_id != 0, "a connected host must report a connection id"

    # Device -> host: the peer notifies, the parser decodes.
    peer.write("n")
    peer.expect_exact("PEER_NOTIFIED note_on length=5", timeout=25)
    dut.expect(
        re.compile(
            rb"MIDI_IN id=%d status=0x90 data1=60 data2=100 length=2 ts=\d+ context=loop"
            % connection_id
        ),
        timeout=25,
    )

    # Running status: the second Note On has no status byte of its own.
    peer.write("r")
    peer.expect_exact("PEER_NOTIFIED running_status length=7", timeout=25)
    dut.expect(re.compile(rb"MIDI_IN id=\d+ status=0x90 data1=60 data2=100"), timeout=25)
    dut.expect(re.compile(rb"MIDI_IN id=\d+ status=0x90 data1=62 data2=101"), timeout=25)

    # An interleaved System Real-Time byte must be delivered on its own and must
    # not clear the running status of the messages around it.
    peer.write("t")
    peer.expect_exact("PEER_NOTIFIED real_time length=9", timeout=25)
    dut.expect(re.compile(rb"MIDI_IN id=\d+ status=0x90 data1=60 data2=100"), timeout=25)
    dut.expect(
        re.compile(rb"MIDI_IN id=\d+ status=0xfe data1=0 data2=0 length=0"), timeout=25
    )
    dut.expect(re.compile(rb"MIDI_IN id=\d+ status=0x90 data1=62 data2=101"), timeout=25)

    # A SysEx over three notifications, reassembled in order.
    peer.write("y")
    peer.expect_exact("PEER_NOTIFIED sysex_first length=14", timeout=25)
    dut.expect(re.compile(rb"MIDI_IN_SYSEX id=\d+ start=1 end=0 length=11"), timeout=25)
    peer.expect_exact("PEER_NOTIFIED sysex_continuation length=21", timeout=25)
    dut.expect(re.compile(rb"MIDI_IN_SYSEX id=\d+ start=0 end=0 length=20"), timeout=25)
    peer.expect_exact("PEER_NOTIFIED sysex_last length=13", timeout=25)
    dut.expect(re.compile(rb"MIDI_IN_SYSEX id=\d+ start=0 end=0 length=10"), timeout=25)
    # The 0xF7 in the last packet is preceded by a timestamp, so the end of the
    # stream is its own chunk carrying no payload.
    dut.expect(re.compile(rb"MIDI_IN_SYSEX id=\d+ start=0 end=1 length=0"), timeout=25)

    dut.write("r")
    dut.expect_exact("RECEIVED messages=6 status=0x90 data1=62 data2=101", timeout=20)
    # 41 bytes: the 0x7D identifier and a 40-byte ramp.
    dut.expect_exact(
        "RECEIVED_SYSEX chunks=4 bytes=41 start=1 end=1 ramp=1", timeout=20
    )

    # Host -> device. Each send waits for the previous write to complete, because a
    # central runs one GATT operation at a time.
    for command, midi in (
        ("n", "903c64"),   # Note On
        ("f", "803c00"),   # Note Off
        ("o", "b30764"),   # Control Change 7 on channel 3
        ("g", "c92a"),     # Program Change 42 on channel 9
    ):
        dut.write(command)
        dut.expect_exact("SEND accepted=1", timeout=20)
        peer.expect(
            re.compile(
                rb"PEER_WRITE length=%d ts=\d+ midi=%s"
                % (len(midi) // 2 + 2, midi.encode())
            ),
            timeout=25,
        )
        dut.expect_exact("WRITE_DONE success=1 context=loop", timeout=25)

    # The library raised the MTU to 247 on connect, so one ATT write carries 244
    # bytes and a 320-byte SysEx has to be split. The second packet is issued from
    # the first write's completion, and nothing else may go out while that runs.
    dut.write("s")
    dut.expect_exact(
        "SEND_SYSEX accepted=1 length=320 second=0 note=0 sending=1", timeout=20
    )
    dut.expect_exact("SYSEX_SENT", timeout=60)
    peer.write("q")
    report = peer.expect(re.compile(rb"PEER_REPORT writes=(\d+) last=\w+"), timeout=20)
    sysex = peer.expect(
        re.compile(
            rb"PEER_WRITE_SYSEX_REPORT packets=(\d+) bytes=318 start=1 end=1 ramp=1"
        ),
        timeout=20,
    )
    packets = int(sysex.group(1))
    assert packets == 2, (
        "318 payload bytes at a 247-byte MTU are two writes, not %s" % packets
    )
    # Every write is accounted for: the four messages above plus the SysEx packets.
    assert int(report.group(1)) == 4 + packets, (
        "unexpected writes on the air: %s" % report.group(1)
    )

    dut.write("x")
    dut.expect_exact("DISCONNECT_REQUESTED 1", timeout=20)
    peer.expect_exact("PEER_DISCONNECTED", timeout=25)
