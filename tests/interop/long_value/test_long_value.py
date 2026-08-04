import re


def test_a_value_above_the_mtu_arrives_whole_from_a_nimble_peripheral(dut, peers):
    """A read longer than the MTU is completed against the other host stack.

    `peer/long_value` already established that Bluedroid continues such a read
    internally instead of truncating it at mtu - 1, which is the opposite of what
    this repository's documentation assumed before that test ran. Both ends of it
    are Bluedroid, though, so what it pins could be two halves of one stack
    agreeing with each other. Here the responder is NimBLE, which makes the claim
    about this library's client.

    Nothing in the API surface promises the continuation, so the assertions are
    the peer's exact value length and every byte against its ramp — for both
    public entry points, since the UUID form and the handle form take different
    paths inside the library.

    `dut` is the ESP32-S3 running EspBle and `peers["device"]` is the original
    ESP32 running the library under test.
    """
    espble = dut
    bluedroid = peers["device"]

    # The EspBle board is asked to report rather than being waited on: it boots
    # while the other board is still being flashed, so the startup line alone
    # would depend on when the monitor started reading.
    espble.write("?")
    state = espble.expect(
        re.compile(rb"ESPBLE_LONG_VALUE_STATE ready=1 length=(\d+)"), timeout=40
    )
    peer_length = int(state.group(1))
    bluedroid.expect_exact("INTEROP_LONG_VALUE_READY", timeout=40)

    bluedroid.write("c")
    bluedroid.expect_exact("SCAN_STARTED 1", timeout=10)
    bluedroid.expect(
        re.compile(rb"TARGET_FOUND address=([0-9a-f:]+) name=EspBle Long Value"),
        timeout=40,
    )
    bluedroid.expect_exact("CONNECT_REQUESTED 1", timeout=10)
    bluedroid.expect(re.compile(rb"CONNECTED id=(\d+) mtu=23"), timeout=25)

    # The negotiated link is the interesting one: at the initial 23 the read would
    # need many more continuations, and the value must still exceed whatever the
    # two stacks settled on or the scenario proves nothing.
    mtu_line = bluedroid.expect(
        re.compile(rb"MTU previous=23 mtu=(\d+) context=loop"), timeout=25
    )
    mtu = int(mtu_line.group(1))
    assert mtu == 247, (
        "expected the preferred MTU of both libraries; got %d" % mtu
    )
    assert peer_length > mtu, (
        "the peer value (%d) must exceed the MTU (%d) for this test to mean "
        "anything" % (peer_length, mtu)
    )

    bluedroid.write("d")
    bluedroid.expect_exact("DISCOVERY_REQUESTED 1", timeout=10)
    discovery = bluedroid.expect(
        re.compile(rb"DISCOVERY success=1 characteristic=(\d+) context=loop"),
        timeout=30,
    )
    assert int(discovery.group(1)) != 0, "the characteristic must be found"

    # Both public entry points return the complete value, byte for byte.
    bluedroid.write("r")
    bluedroid.expect_exact("UUID_READ_REQUESTED 1", timeout=10)
    uuid_read = bluedroid.expect(
        re.compile(
            rb"READ success=1 error=NONE length=(\d+) mtu=(\d+) ramp=1 "
            rb"context=loop"
        ),
        timeout=25,
    )
    assert int(uuid_read.group(2)) == mtu
    assert int(uuid_read.group(1)) == peer_length, (
        "the UUID form returned %s of %d bytes; a value longer than the MTU must "
        "not come back truncated" % (uuid_read.group(1).decode(), peer_length)
    )

    bluedroid.write("R")
    bluedroid.expect_exact("HANDLE_READ_REQUESTED 1", timeout=10)
    handle_read = bluedroid.expect(
        re.compile(
            rb"READ success=1 error=NONE length=(\d+) mtu=(\d+) ramp=1 "
            rb"context=loop"
        ),
        timeout=25,
    )
    assert int(handle_read.group(2)) == mtu
    assert int(handle_read.group(1)) == peer_length, (
        "the handle form must return the same complete value as the UUID form"
    )

    bluedroid.write("x")
    bluedroid.expect_exact("DISCONNECT_REQUESTED 1", timeout=10)
    bluedroid.expect(
        re.compile(rb"DISCONNECTED id=\d+ dropped=0 context=loop"), timeout=25
    )
