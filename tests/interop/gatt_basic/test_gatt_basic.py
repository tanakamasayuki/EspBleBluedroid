import re


def test_bluedroid_central_against_an_espble_peripheral(dut, peers):
    """The whole basic GATT vocabulary across two different host stacks.

    Everything here also has a same-stack counterpart in `peer/`. What this adds
    is the other end: a NimBLE peripheral built from a released EspBle, pinned by
    version in its `sketch.yaml`. An assumption that happens to hold only because
    both sides are Bluedroid shows up here and nowhere else, so the assertions are
    the procedure and the exact bytes rather than "it worked".

    `dut` is the ESP32-S3 running EspBle and `peers["device"]` is the original
    ESP32 running the library under test. Each step is issued by a serial command,
    so a failure names the step.
    """
    espble = dut
    bluedroid = peers["device"]

    # The EspBle board is asked to report rather than being waited on: it boots
    # while the other board is still being flashed, so the startup line alone
    # would depend on when the monitor started reading.
    espble.write("?")
    espble.expect_exact("ESPBLE_PERIPHERAL_READY advertising=1", timeout=40)
    bluedroid.expect_exact("INTEROP_GATT_BASIC_READY", timeout=40)

    # Connect. The peer is chosen by its service UUID; its advertised name is
    # captured only as information.
    bluedroid.write("c")
    bluedroid.expect_exact("SCAN_STARTED 1", timeout=10)
    bluedroid.expect(
        re.compile(rb"TARGET_FOUND address=([0-9a-f:]+) name=EspBle Interop Peer"),
        timeout=40,
    )
    bluedroid.expect_exact("CONNECT_REQUESTED 1", timeout=10)
    bluedroid.expect(re.compile(rb"CONNECTED id=(\d+) mtu=23"), timeout=25)
    espble.expect(re.compile(rb"ESPBLE_CONNECTED id=\d+ mtu=\d+"), timeout=20)

    # Both libraries ask for 247, so a cross-stack link has to reach it. A lower
    # value would mean one side is not requesting what it says it prefers.
    mtu = bluedroid.expect(
        re.compile(rb"MTU previous=23 mtu=(\d+) context=loop"), timeout=25
    )
    assert int(mtu.group(1)) == 247, (
        "expected the preferred MTU of both libraries; got %s"
        % mtu.group(1).decode()
    )
    espble.expect(re.compile(rb"ESPBLE_MTU mtu=247"), timeout=20)

    # Discovery: the peer's attribute layout and declared properties have to
    # survive the trip.
    bluedroid.write("d")
    bluedroid.expect_exact("DISCOVERY_REQUESTED 1", timeout=10)
    discovery = bluedroid.expect(
        re.compile(
            rb"DISCOVERY success=1 services=(\d+) characteristic=(\d+) "
            rb"descriptor=(\d+) properties=1 context=loop"
        ),
        timeout=30,
    )
    assert int(discovery.group(1)) >= 1
    assert int(discovery.group(2)) != 0, "the characteristic must be found"
    assert int(discovery.group(3)) != 0, "the descriptor must be found"

    # Read the value the NimBLE peripheral published.
    bluedroid.write("r")
    bluedroid.expect_exact("READ_REQUESTED 1", timeout=10)
    bluedroid.expect_exact(
        "READ success=1 error=None length=4 hex=1a00fe2b context=loop", timeout=25
    )

    # Write with a response, then without: the peer must see the same bytes both
    # times, and only the first is acknowledged on this side.
    bluedroid.write("w")
    bluedroid.expect_exact("WRITE_REQUESTED 1", timeout=10)
    bluedroid.expect_exact(
        "WRITE success=1 error=None response=1 context=loop", timeout=25
    )
    espble.expect_exact("ESPBLE_WRITE length=4 hex=2a00fa5e context=loop", timeout=20)

    bluedroid.write("W")
    bluedroid.expect_exact("WRITE_NO_RESPONSE_REQUESTED 1", timeout=10)
    bluedroid.expect_exact(
        "WRITE success=1 error=None response=0 context=loop", timeout=25
    )
    espble.expect_exact("ESPBLE_WRITE length=4 hex=2a00fa5e context=loop", timeout=20)

    # Descriptor Read and Write, addressed by handle.
    bluedroid.write("e")
    bluedroid.expect_exact("DESCRIPTOR_READ_REQUESTED 1", timeout=10)
    bluedroid.expect_exact(
        "DESCRIPTOR_READ success=1 error=None length=3 hex=1d00fd context=loop",
        timeout=25,
    )
    bluedroid.write("f")
    bluedroid.expect_exact("DESCRIPTOR_WRITE_REQUESTED 1", timeout=10)
    bluedroid.expect_exact(
        "DESCRIPTOR_WRITE success=1 error=None context=loop", timeout=25
    )

    # Notification: CCCD 0x0001 written by one stack, delivered by the other.
    bluedroid.write("s")
    bluedroid.expect_exact("SUBSCRIBE_REQUESTED 1", timeout=10)
    bluedroid.expect_exact(
        "SUBSCRIBED success=1 notifications=1 indications=0 context=loop",
        timeout=25,
    )
    espble.expect_exact(
        "ESPBLE_SUBSCRIPTION notifications=1 indications=0", timeout=20
    )
    espble.write("n")
    espble.expect_exact("ESPBLE_NOTIFY_ACCEPTED 1", timeout=10)
    bluedroid.expect(
        re.compile(
            rb"NOTIFICATION indication=0 handle=\d+ length=4 hex=1c00fc3c "
            rb"context=loop"
        ),
        timeout=25,
    )

    # Indication: the same CCCD with the other bit, and a confirmation the peer
    # has to observe.
    bluedroid.write("S")
    bluedroid.expect_exact("SUBSCRIBE_INDICATIONS_REQUESTED 1", timeout=10)
    bluedroid.expect_exact(
        "SUBSCRIBED success=1 notifications=0 indications=1 context=loop",
        timeout=25,
    )
    espble.expect_exact(
        "ESPBLE_SUBSCRIPTION notifications=0 indications=1", timeout=20
    )
    espble.write("i")
    espble.expect_exact("ESPBLE_INDICATE_ACCEPTED 1", timeout=10)
    bluedroid.expect(
        re.compile(
            rb"NOTIFICATION indication=1 handle=\d+ length=4 hex=1e00fb4d "
            rb"context=loop"
        ),
        timeout=25,
    )
    espble.expect_exact("ESPBLE_SENT success=1 indication=1", timeout=20)

    # Unsubscribe and close, leaving nothing behind on either side.
    bluedroid.write("u")
    bluedroid.expect_exact("UNSUBSCRIBE_REQUESTED 1", timeout=10)
    bluedroid.expect_exact("UNSUBSCRIBED success=1 context=loop", timeout=25)
    espble.expect_exact(
        "ESPBLE_SUBSCRIPTION notifications=0 indications=0", timeout=20
    )

    bluedroid.write("x")
    bluedroid.expect_exact("DISCONNECT_REQUESTED 1", timeout=10)
    bluedroid.expect(
        re.compile(rb"DISCONNECTED id=\d+ dropped=0 context=loop"), timeout=25
    )
    espble.expect(re.compile(rb"ESPBLE_DISCONNECTED id=\d+"), timeout=20)
