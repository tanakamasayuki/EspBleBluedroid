import re


def test_the_primary_and_the_listeners_all_see_one_event(dut, peers):
    """`add*Listener()`: several observers on one event, in a fixed order.

    One event used to have exactly one owner, so a profile helper — the BLE MIDI
    and HID helpers are why this matters — could only work by taking the slot the
    application wanted. The contract asserted here is EspBle's, because a helper
    written against either library has to behave the same: the primary first, then
    the listeners in registration order, four listeners per list, ids unique
    across the lists of one object, and a listener added during a dispatch left
    out of that dispatch.

    The radio half runs in the peripheral role against a raw Arduino-ESP32
    central, so the connection list, the Server lists and the send-completion list
    are driven by real events rather than by injection.
    """
    peer = peers["device"]
    dut.expect_exact("MULTI_LISTENER_READY", timeout=30)
    peer.expect_exact("MULTI_LISTENER_PEER_READY", timeout=30)

    # The registration contract, captured while registering.
    dut.write("r")
    # `server_ids` is the GATT Server's own id space: ids are unique per owner
    # rather than globally, so the Server's remover owns them and a removed id is
    # gone for good.
    dut.expect_exact(
        "REGISTRATION accepted=3 fifth_refused=1 empty_refused=1 removed=1 "
        "unknown_failed=1 wrong_family_failed=1 right_family=1 server_ids=1",
        timeout=20,
    )
    ids = dut.expect(
        re.compile(rb"IDS first=(\d+) second=(\d+) distinct=1"), timeout=20
    )
    assert int(ids.group(1)) != 0 and int(ids.group(2)) != 0, (
        "an accepted listener must return a usable id"
    )

    # The connection itself is only visible through the tags the observers append,
    # which is what the ORDER line below reports.
    peer.write("c")
    peer.expect_exact("PEER_CONNECTED characteristic=1", timeout=40)

    # Subscribe, write, notify: one event per list, all through update().
    peer.write("s")
    peer.expect_exact("PEER_SUBSCRIBED", timeout=25)
    dut.expect_exact("SUBSCRIPTION notifications=1 context=loop", timeout=25)
    peer.write("w")
    peer.expect_exact("PEER_WRITTEN", timeout=25)
    dut.expect(
        re.compile(rb"WRITE id=\d+ value=peer-write context=loop"), timeout=25
    )
    dut.write("n")
    dut.expect_exact("NOTIFY_ACCEPTED 1", timeout=20)
    dut.expect_exact("SENT success=1 context=loop", timeout=25)
    peer.expect_exact("PEER_NOTIFICATION value=listener-notify", timeout=25)

    # The order is the assertion. `connected` misses L2 because it was removed and
    # misses `late` because that listener was added from inside the dispatch;
    # both would show up here if either rule were broken.
    dut.write("o")
    dut.expect_exact(
        "ORDER connected=primary,L1,L3,L4 written=primary,L1,L2 "
        "subscription=primary,L1 sent=primary,L1",
        timeout=20,
    )

    peer.write("x")
    peer.expect_exact("PEER_DISCONNECT_REQUESTED", timeout=20)
