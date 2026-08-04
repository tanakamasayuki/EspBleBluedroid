import re


def test_duplicate_uuids_published_by_a_nimble_peripheral(dut, peers):
    """Two characteristics sharing a UUID, addressed by handle across stacks.

    The spec allows the duplicates, this library's server API rejects them (it
    addresses local attributes by UUID), and its client half still has to work
    with a peer that publishes them. `peer/duplicate_uuid` pins both halves
    against the bundled Arduino Bluedroid wrapper; here the responder is NimBLE,
    so the routing is checked against a second implementation rather than the
    stack this library sits on.

    Every operation is attributed: reads and the write report the handle they
    completed on, and the peer reports which of its two attributes a write landed
    on, so a routing error cannot hide behind a value that happens to match.

    `dut` is the ESP32-S3 running EspBle and `peers["device"]` is the original
    ESP32 running the library under test.
    """
    espble = dut
    bluedroid = peers["device"]

    # The EspBle board is asked to report rather than being waited on: it boots
    # while the other board is still being flashed, so the startup line alone
    # would depend on when the monitor started reading. `distinct=1` is the
    # premise: the peer registered two attributes instead of reusing the first.
    espble.write("?")
    espble.expect_exact("ESPBLE_DUPLICATE_STATE ready=1 distinct=1", timeout=40)
    bluedroid.expect_exact("INTEROP_DUPLICATE_UUID_READY", timeout=40)

    # The server-side restriction, recorded next to the client behaviour it
    # forces. Same error string as `peer/duplicate_uuid` asserts.
    bluedroid.write("r")
    bluedroid.expect_exact("LOCAL_BASE_ACCEPTED 1", timeout=20)
    bluedroid.expect_exact(
        "LOCAL_DUPLICATE_REJECTED 1 error=INVALID_ARGUMENT "
        "detail=this library cannot address duplicate Characteristic UUIDs in "
        "one Service",
        timeout=20,
    )

    bluedroid.write("c")
    bluedroid.expect_exact("SCAN_STARTED 1", timeout=10)
    bluedroid.expect(
        re.compile(
            rb"TARGET_FOUND address=([0-9a-f:]+) name=EspBle Duplicate UUID"
        ),
        timeout=40,
    )
    bluedroid.expect_exact("CONNECT_REQUESTED 1", timeout=10)
    bluedroid.expect(re.compile(rb"CONNECTED id=\d+"), timeout=25)

    # Discovery has to keep both attributes apart. Two entries with one UUID and
    # two different handles is the whole reason the handle forms exist.
    bluedroid.write("d")
    bluedroid.expect_exact("DISCOVERY_REQUESTED 1", timeout=10)
    discovery = bluedroid.expect(
        re.compile(
            rb"DISCOVERY success=1 matches=(\d+) first=(\d+) second=(\d+) "
            rb"distinct=1 context=loop"
        ),
        timeout=30,
    )
    assert int(discovery.group(1)) == 2, (
        "a client that collapses duplicates by UUID would report %s"
        % discovery.group(1).decode()
    )
    first_handle = int(discovery.group(2))
    second_handle = int(discovery.group(3))

    # The UUID form can only reach one of the two, and which one is part of the
    # contract: the first entry in the discovery snapshot.
    bluedroid.write("u")
    bluedroid.expect_exact("UUID_READ_REQUESTED 1", timeout=10)
    bluedroid.expect_exact(
        "READ success=1 error=NONE handle=%d which=1 hex=4100f1 context=loop"
        % first_handle,
        timeout=25,
    )

    # Both attributes by handle. The values differ, so a read that reached the
    # wrong one fails here rather than merely being unproven.
    bluedroid.write("1")
    bluedroid.expect_exact("FIRST_READ_REQUESTED 1", timeout=10)
    bluedroid.expect_exact(
        "READ success=1 error=NONE handle=%d which=1 hex=4100f1 context=loop"
        % first_handle,
        timeout=25,
    )
    bluedroid.write("2")
    bluedroid.expect_exact("SECOND_READ_REQUESTED 1", timeout=10)
    bluedroid.expect_exact(
        "READ success=1 error=NONE handle=%d which=2 hex=4200f2 context=loop"
        % second_handle,
        timeout=25,
    )

    # Write to the second attribute: the peer must see it on its second
    # characteristic, which is the same routing question in the other direction.
    bluedroid.write("w")
    bluedroid.expect_exact("SECOND_WRITE_REQUESTED 1", timeout=10)
    bluedroid.expect_exact(
        "WRITE success=1 error=NONE handle=%d which=2 context=loop"
        % second_handle,
        timeout=25,
    )
    espble.expect_exact("ESPBLE_WRITE which=2 hex=4400f4", timeout=20)

    # Subscribe on the second attribute's CCCD and receive from it. The
    # notification has to arrive tagged with the second handle.
    bluedroid.write("s")
    bluedroid.expect_exact("SECOND_SUBSCRIBE_REQUESTED 1", timeout=10)
    bluedroid.expect_exact(
        "SUBSCRIBED success=1 handle=%d which=2 context=loop" % second_handle,
        timeout=25,
    )
    espble.expect_exact(
        "ESPBLE_SUBSCRIPTION which=2 notifications=1 indications=0", timeout=20
    )
    espble.write("n")
    espble.expect_exact("ESPBLE_NOTIFY_ACCEPTED 1", timeout=10)
    bluedroid.expect_exact(
        "NOTIFICATION handle=%d which=2 hex=4300f3 context=loop" % second_handle,
        timeout=25,
    )

    bluedroid.write("x")
    bluedroid.expect_exact("DISCONNECT_REQUESTED 1", timeout=10)
    bluedroid.expect(
        re.compile(rb"DISCONNECTED id=\d+ dropped=0 context=loop"), timeout=25
    )
