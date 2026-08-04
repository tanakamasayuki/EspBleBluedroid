import re


def test_read_above_the_mtu_returns_the_whole_value(dut, peers):
    """A value longer than the MTU is read completely, not cut short.

    Bluedroid exposes no Read Blob call, so this could easily have been a
    truncating read (and this repository's documentation assumed it was until
    this test ran on hardware). Bluedroid continues the read internally instead,
    which matches EspBle's NimBLE behaviour. Nothing in the API promises that, so
    the agreement is pinned here: the assertion is the peer's exact value length,
    and every byte is compared against the peer's ramp.
    """
    peer = peers["device"]
    peer_ready = peer.expect(
        re.compile(rb"LONG_VALUE_PEER_READY length=(\d+)"), timeout=30
    )
    peer_length = int(peer_ready.group(1))
    dut.expect_exact("LONG_VALUE_READY", timeout=30)
    dut.expect(re.compile(rb"TARGET_FOUND ([0-9a-f:]+)"), timeout=30)
    dut.expect_exact("CONNECT_REQUESTED 1", timeout=10)
    dut.expect(re.compile(rb"CONNECTED id=\d+ mtu=23"), timeout=20)

    mtu_line = dut.expect(re.compile(rb"MTU mtu=(\d+) context=loop"), timeout=20)
    mtu = int(mtu_line.group(1))
    assert mtu > 23, "the link never left the default MTU, so nothing is proven"
    assert peer_length > mtu, (
        "the peer value (%d) must exceed the MTU (%d) for this test to mean "
        "anything" % (peer_length, mtu)
    )
    dut.expect_exact("DISCOVERY_REQUESTED 1", timeout=10)

    discovery = dut.expect(
        re.compile(rb"DISCOVERY success=1 handle=(\d+)"), timeout=20
    )
    assert int(discovery.group(1)) != 0
    dut.expect_exact("UUID_READ_REQUESTED 1", timeout=10)

    # Both public entry points return the complete value, byte for byte.
    uuid_read = dut.expect(
        re.compile(
            rb"UUID_READ success=1 length=(\d+) mtu=(\d+) ramp=1 context=loop"
        ),
        timeout=20,
    )
    assert int(uuid_read.group(2)) == mtu
    assert int(uuid_read.group(1)) == peer_length, (
        "the UUID form returned %s of %d bytes; a value longer than the MTU must "
        "not come back truncated" % (uuid_read.group(1).decode(), peer_length)
    )
    dut.expect_exact("HANDLE_READ_REQUESTED 1", timeout=10)

    handle_read = dut.expect(
        re.compile(
            rb"HANDLE_READ success=1 length=(\d+) mtu=(\d+) ramp=1 context=loop"
        ),
        timeout=20,
    )
    assert int(handle_read.group(2)) == mtu
    assert int(handle_read.group(1)) == peer_length, (
        "the handle form must return the same complete value as the UUID form"
    )

    dut.expect(re.compile(rb"PEER_VALUE_LENGTH %d" % peer_length), timeout=10)
    dut.expect(
        re.compile(rb"DISCONNECTED id=\d+ dropped=0 context=loop"), timeout=20
    )
