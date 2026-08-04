import re


def test_duplicate_uuids_are_registered_locally_and_handled_remotely(dut, peers):
    """Both halves of the duplicate-UUID contract, in one connection.

    The server half is registration: a second characteristic with the same UUID in
    one service is accepted and gets a handle of its own, which is what HID over
    GATT needs (its Report characteristics all carry 0x2a4d). Equal handles would
    mean the backend had reused the first entry, so `distinct=1` is the real
    assertion. A duplicate *descriptor* is still refused, and for a reason that
    does not go away: a descriptor is looked up by UUID inside its characteristic.
    What the accepted pair looks like on the air is `peer/duplicate_uuid_server`.

    The client half is the opposite requirement: a peer is allowed to publish
    duplicates, so the handle-addressed operations must reach each one and route
    notifications to the handle that sent them.
    """
    peer = peers["device"]

    peer_ready = peer.expect(
        re.compile(
            rb"DUPLICATE_UUID_PEER_READY first=(\d+) second=(\d+) distinct=1"
        ),
        timeout=30,
    )
    peer_first = int(peer_ready.group(1))
    peer_second = int(peer_ready.group(2))

    dut.expect_exact("DUPLICATE_UUID_READY", timeout=30)

    # Server side: the base registration is accepted, the duplicate characteristic
    # is accepted with its own handle, the duplicate descriptor is not, and the
    # same UUID in another Service is. The sketch reports on request rather than
    # from setup(), because output from the first second after flashing can be lost
    # while the serial port reopens.
    dut.write("r")
    dut.expect_exact("LOCAL_BASE_ACCEPTED 1", timeout=20)
    dut.expect_exact(
        "DUPLICATE_CHARACTERISTIC_ACCEPTED 1 distinct=1 error=NONE", timeout=10
    )
    dut.expect_exact(
        "DUPLICATE_DESCRIPTOR_REJECTED 1 error=INVALID_ARGUMENT "
        "detail=duplicate Descriptor UUID in one Characteristic",
        timeout=10,
    )
    dut.expect_exact("SAME_UUID_OTHER_SERVICE_ACCEPTED 1", timeout=10)
    # A malformed UUID must fail the registration call, not begin(). The wrapper
    # crashes on an unset BLEUUID while creating the database, which is a boot
    # loop with a backtrace nowhere near the offending addService().
    dut.expect_exact(
        "MALFORMED_UUID_REJECTED service=1 characteristic=1 "
        "error=INVALID_ARGUMENT detail=invalid GATT Service UUID",
        timeout=10,
    )

    # Client side against the peer's two same-UUID characteristics.
    dut.write("c")
    dut.expect_exact("SCAN_STARTED 1", timeout=10)
    dut.expect(re.compile(rb"TARGET_FOUND ([0-9a-f:]+)"), timeout=30)
    dut.expect_exact("CONNECT_REQUESTED 1", timeout=10)
    dut.expect(re.compile(rb"CONNECTED id=\d+"), timeout=20)
    dut.expect_exact("DISCOVERY_REQUESTED 1", timeout=10)

    discovery = dut.expect(
        re.compile(
            rb"DISCOVERY success=1 matches=(\d+) first=(\d+) second=(\d+) "
            rb"distinct=1 context=loop"
        ),
        timeout=20,
    )
    assert int(discovery.group(1)) == 2, (
        "the discovery snapshot must keep both characteristics, not collapse "
        "them by UUID"
    )
    # The handles the client discovered are the handles the peer created.
    assert int(discovery.group(2)) == peer_first
    assert int(discovery.group(3)) == peer_second

    dut.expect_exact("FIRST_READ_REQUESTED 1", timeout=10)
    dut.expect_exact(
        "FIRST_READ success=1 handle=%d byte=a1 context=loop" % peer_first,
        timeout=20,
    )
    dut.expect_exact("SECOND_READ_REQUESTED 1", timeout=10)
    dut.expect_exact(
        "SECOND_READ success=1 handle=%d byte=b2 context=loop" % peer_second,
        timeout=20,
    )

    dut.expect_exact("SUBSCRIBE_REQUESTED 1", timeout=10)
    dut.expect_exact(
        "SUBSCRIBED success=1 handle=%d context=loop" % peer_second, timeout=20
    )

    # Only the second characteristic notifies; the value has to arrive against
    # that handle.
    peer.write("n")
    peer.expect_exact("PEER_NOTIFIED handle=%d" % peer_second, timeout=20)
    dut.expect_exact(
        "NOTIFICATION handle=%d second=1 byte=b2 length=2 context=loop"
        % peer_second,
        timeout=20,
    )

    dut.write("d")
    dut.expect_exact("DISCONNECT_REQUESTED 1", timeout=10)
    dut.expect(re.compile(rb"DISCONNECTED id=\d+ context=loop"), timeout=20)
