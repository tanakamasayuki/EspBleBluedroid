import re


def test_two_same_uuid_characteristics_are_published_and_addressable(dut, peers):
    """The published database really carries both duplicates, not one twice.

    `peer/duplicate_uuid` shows that registration accepts a second characteristic
    with the same UUID and hands out a second handle. That is not enough: a backend
    that reused the first entry would pass those assertions too, because every
    local call would still succeed. The claim can only be settled from the outside,
    which is what this suite does — and it is the prerequisite HID over GATT rests
    on, since a keyboard's Report characteristics all carry UUID 0x2a4d.

    The peer is the raw Arduino-ESP32 wrapper. Its UUID-keyed map can only return
    one of a duplicated pair, so it walks the handle-keyed map instead; that is
    also the honest statement of what any client must do here.
    """
    peer = peers["device"]
    dut.write("?")
    dut.expect_exact("READY_STATE started=1", timeout=30)
    peer.expect_exact("DUPLICATE_SERVER_PEER_READY", timeout=30)

    dut.write("r")
    dut.expect_exact("REGISTRATION accepted=2 distinct=1 descriptors=2", timeout=20)

    peer.write("c")
    peer.expect_exact("PEER_CONNECTED service=1", timeout=40)
    peer.write("d")
    discovery = peer.expect(
        re.compile(
            rb"PEER_DISCOVERY matches=2 distinct=1 first=(\d+) second=(\d+)"
        ),
        timeout=30,
    )
    first_handle = int(discovery.group(1))
    second_handle = int(discovery.group(2))
    assert first_handle and second_handle and first_handle != second_handle, (
        "two characteristics need two handles: %d and %d"
        % (first_handle, second_handle)
    )

    # Different values behind one UUID. One attribute reached twice would report
    # the same value on both handles.
    peer.write("r")
    peer.expect_exact("PEER_READ first=first-value second=second-value", timeout=30)
    # The HID shape: each one carries its own Report-Reference-style descriptor.
    peer.write("D")
    peer.expect_exact("PEER_DESCRIPTOR first=report-a second=report-b", timeout=30)

    # A write aimed at the second handle must be attributed to the second
    # characteristic, not to the UUID's first match.
    peer.write("w")
    peer.expect_exact("PEER_WRITTEN", timeout=25)
    dut.expect_exact("WRITE id=2 value=peer-to-second context=loop", timeout=25)

    # Two CCCDs, one per characteristic.
    peer.write("s")
    peer.expect_exact("PEER_SUBSCRIBED", timeout=30)
    dut.expect_exact("SUBSCRIPTION id=1 notifications=1 context=loop", timeout=25)
    dut.expect_exact("SUBSCRIPTION id=2 notifications=1 context=loop", timeout=25)

    # Each notification has to arrive on the handle that sent it.
    dut.write("1")
    dut.expect_exact("NOTIFY_ACCEPTED id=1 1", timeout=20)
    dut.expect_exact("SENT id=1 success=1 context=loop", timeout=25)
    peer.expect_exact(
        "PEER_NOTIFICATION handle=%d value=notify-a" % first_handle, timeout=25
    )
    dut.write("2")
    dut.expect_exact("NOTIFY_ACCEPTED id=2 1", timeout=20)
    dut.expect_exact("SENT id=2 success=1 context=loop", timeout=25)
    peer.expect_exact(
        "PEER_NOTIFICATION handle=%d value=notify-b" % second_handle, timeout=25
    )

    peer.write("x")
    peer.expect_exact("PEER_DISCONNECT_REQUESTED", timeout=20)
