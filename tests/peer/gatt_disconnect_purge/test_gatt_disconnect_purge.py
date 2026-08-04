import re


def test_in_flight_read_completes_once_when_the_link_is_torn_down(dut, peers):
    """`disconnect()` during a GATT read: one completion, then a usable stack.

    The failure this guards against is silent. If the in-flight operation
    produced no completion, an application waiting for its callback would hang
    with no error; if it produced two, a state machine keyed on "one result per
    request" would take a step it never asked for; if the operation slot or the
    ATT resources stayed held, the *next* connection would fail to discover for
    reasons that point nowhere near the disconnect.

    Whether that read succeeds or fails is a race with the link teardown, so it
    is deliberately not asserted — the count is.
    """
    peer = peers["device"]
    peer_ready = peer.expect(
        re.compile(rb"DISCONNECT_PURGE_PEER_READY length=(\d+)"), timeout=30
    )
    value_length = int(peer_ready.group(1))
    dut.expect_exact("GATT_DISCONNECT_PURGE_READY", timeout=30)

    dut.write("c")
    dut.expect_exact("SCAN_STARTED 1", timeout=10)
    dut.expect(re.compile(rb"TARGET_FOUND ([0-9a-f:]+)"), timeout=30)
    dut.expect_exact("CONNECT_REQUESTED 1", timeout=10)
    dut.expect(re.compile(rb"CONNECTED generation=1 id=\d+"), timeout=20)
    dut.expect_exact("DISCOVERY_REQUESTED 1", timeout=10)
    discovery = dut.expect(
        re.compile(rb"DISCOVERY generation=1 success=1 handle=(\d+) context=loop"),
        timeout=20,
    )
    assert int(discovery.group(1)) != 0

    dut.expect_exact("READ_REQUESTED generation=1 1", timeout=10)
    # Accepted, not rejected: an application that has decided to disconnect must
    # not be told the link is still up because a read happens to be running.
    dut.expect_exact("DISCONNECT_DURING_READ_ACCEPTED 1", timeout=10)

    # The completion and the disconnection race each other, so both are awaited
    # without fixing an order. Whether the read managed to succeed before the link
    # went away is a race too; what is not negotiable is that a completion arrives
    # and that a failure says why.
    results = dut.expect(
        [
            re.compile(
                rb"READ_RESULT generation=1 count=1 success=(\d) error=(\S+) "
                rb"length=\d+ detail=(.*) context=loop"
            ),
            re.compile(
                rb"DISCONNECTED generation=1 id=\d+ during_read=1 results=\d+ "
                rb"dropped=\d+ context=loop"
            ),
        ],
        expect_all=True,
        timeout=25,
    )
    read = next(match for match in results if match.re.pattern.startswith(b"READ_RESULT"))
    if read.group(1) == b"0":
        assert read.group(2) == b"InvalidState", (
            "a completion that failed because the link went away has to say so"
        )
        assert read.group(3).strip() == (
            b"connection closed before the GATT operation completed"
        )

    dut.write("s")
    state = dut.expect(
        re.compile(rb"STATE connections=(\d+) dropped=(\d+) results=(\d+)"),
        timeout=20,
    )
    assert int(state.group(1)) == 0, "the link must be gone, not half-closed"
    assert int(state.group(2)) == 0, (
        "a dropped event would mean a completion was produced and then lost, "
        "which the result count below could not tell apart from one that was "
        "never produced"
    )
    assert int(state.group(3)) == 1, (
        "exactly one completion for the one in-flight read: %s arrived"
        % state.group(3).decode()
    )

    # The operation slot and the ATT resources have to be free again, which only
    # the next connection can show.
    dut.write("c")
    dut.expect_exact("SCAN_STARTED 1", timeout=10)
    dut.expect(re.compile(rb"CONNECTED generation=2 id=\d+"), timeout=30)
    dut.expect(
        re.compile(rb"DISCOVERY generation=2 success=1 handle=\d+ context=loop"),
        timeout=20,
    )
    dut.expect_exact("READ_REQUESTED generation=2 1", timeout=10)
    second = dut.expect(
        re.compile(
            rb"READ_RESULT generation=2 count=2 success=1 error=None "
            rb"length=(\d+) detail=none context=loop"
        ),
        timeout=20,
    )
    assert int(second.group(1)) == value_length
