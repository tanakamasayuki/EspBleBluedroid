import re


def test_a_peer_connecting_to_the_server_is_a_reported_connection(dut, peers):
    """The peripheral half of the connection lifecycle.

    A peer that connected to this device's GATT Server used to exist on the air
    and nowhere in the API: no `onConnected()`, no MTU event, no entry in
    `connection()`, and its pairing was dropped because the security callback
    required an active *central* link. This pins the fix — everything observable
    about a link this device opened is observable about a link opened to it, with
    `localRole = Peripheral`.

    The peer is a raw Arduino-ESP32 BLE client, so nothing here is two halves of
    this library agreeing with each other.
    """
    peer = peers["device"]
    dut.expect_exact("PERIPHERAL_LINK_READY", timeout=30)
    peer.expect_exact("PERIPHERAL_LINK_PEER_READY", timeout=30)

    # A bond left by an earlier run would encrypt the link before this run's
    # pairing and hide the security event being checked. Both sides forget: keys
    # kept by only one of them make the next attempt fail instead of pairing.
    dut.write("x")
    dut.expect(re.compile(rb"BONDS_CLEARED success=1 count=\d"), timeout=20)
    peer.write("z")
    peer.expect(re.compile(rb"PEER_BONDS_CLEARED removed=\d+ count=0"), timeout=20)

    peer.write("c")
    peer.expect_exact("PEER_CONNECTED service=1", timeout=40)
    connected = dut.expect(
        re.compile(
            rb"CONNECTED id=(\d+) role=peripheral mtu=23 peer=([0-9a-f:]+) "
            rb"encrypted=0 context=loop"
        ),
        timeout=30,
    )
    connection_id = int(connected.group(1))
    assert connection_id != 0, "a peripheral link needs a runtime ID like any other"

    # This peer asks for SC bonding, so it pairs as soon as it connects. Pairing on
    # a peripheral link used to be dropped entirely, leaving an application unable
    # to tell an encrypted link from a plain one on this side.
    dut.expect(
        re.compile(
            rb"SECURITY success=1 encrypted=1 authenticated=0 bonded=1 key=16 "
            rb"role=peripheral id=%d context=loop" % connection_id
        ),
        timeout=40,
    )
    dut.write("e")
    dut.expect_exact(
        "SECURITY_STATE reported=1 encrypted=1 bonded=1 role=peripheral "
        "matches=1",
        timeout=20,
    )

    # The central drives the MTU exchange; this side only observes it, which it
    # could not do at all before.
    peer.write("m")
    peer_mtu = peer.expect(
        re.compile(rb"PEER_MTU requested=1 mtu=(\d+)"), timeout=25
    )
    mtu = dut.expect(
        re.compile(rb"MTU previous=23 mtu=(\d+) role=peripheral context=loop"),
        timeout=30,
    )
    assert int(mtu.group(1)) == int(peer_mtu.group(1)), (
        "the peripheral reported MTU %s while the central negotiated %s"
        % (mtu.group(1).decode(), peer_mtu.group(1).decode())
    )

    # The snapshot, looked up by the ID the connect event carried.
    dut.write("s")
    snapshot = dut.expect(
        re.compile(
            rb"SNAPSHOT count=(\d+) found=1 id=(\d+) role=peripheral mtu=(\d+) "
            rb"peer=([0-9a-f:]+) interval=(\d+) encrypted=1 bonded=1"
        ),
        timeout=20,
    )
    assert int(snapshot.group(1)) == 1
    assert int(snapshot.group(2)) == connection_id
    assert int(snapshot.group(3)) == int(mtu.group(1))
    assert snapshot.group(4) == connected.group(2), (
        "the snapshot and the connect event must name the same peer"
    )
    assert int(snapshot.group(5)) != 0, (
        "the connection parameters of a peripheral link have to be readable too"
    )

    # A Server event has to carry the same ID, or an application cannot look up
    # the link a write came from.
    peer.write("w")
    peer.expect_exact("PEER_WRITTEN", timeout=25)
    dut.expect_exact("WRITE id=%d value=peer-write context=loop" % connection_id,
                     timeout=20)
    dut.write("w")
    dut.expect_exact("WRITE_ID id=%d matches=1" % connection_id, timeout=20)

    # The encrypted attribute is reachable on the paired link, which is what the
    # reported encryption state claims.
    peer.write("r")
    peer.expect_exact("PEER_ENCRYPTED_READ value=encrypted-ready", timeout=25)

    # Disconnection reports the role, the HCI reason, and leaves the snapshot
    # empty.
    peer.write("x")
    peer.expect_exact("PEER_DISCONNECT_REQUESTED", timeout=20)
    disconnected = dut.expect(
        re.compile(
            rb"DISCONNECTED id=%d role=peripheral reason=(\d+) count=0 "
            rb"context=loop" % connection_id
        ),
        timeout=30,
    )
    assert int(disconnected.group(1)) != 0, (
        "the HCI disconnection reason has to survive to this side as well"
    )
    dut.write("s")
    dut.expect(
        re.compile(rb"SNAPSHOT count=0 found=0 "), timeout=20
    )
