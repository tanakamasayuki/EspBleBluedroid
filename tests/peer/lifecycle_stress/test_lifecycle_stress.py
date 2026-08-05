import re

CYCLES = 8
# Drift allowed between the second round and the last, in bytes. The first round pays
# for one-time allocations, so it can never be the baseline; the number here is set
# from what the hardware actually does, with margin, and a real per-connection leak is
# far larger than it (a few hundred bytes per round compounds to thousands).
MAX_HEAP_DRIFT = 4096
# HCI "connection terminated by local host": the round ended because the sketch asked,
# which is what makes its heap numbers mean anything. A link that dropped on its own
# would leave a different reason and a half-finished teardown.
LOCAL_HOST_TERMINATED = 0x16
CYCLE = re.compile(
    rb"CYCLE (\d+) ok=(\d+) ended=(\d+) stage=(\w+) reason=(\d+) heap=(\d+) "
    rb"minheap=(\d+) tasks=(\d+) dropped=(\d+) context=(\w+)"
)


def test_repeated_lifecycle_leaks_nothing(dut, peers):
    """Eight full lifecycles: nothing accumulates across begin() / end().

    Every other peer suite brings the stack up once. This one runs
    begin() → scan → connect → discover → read → subscribe → write → notification →
    disconnect → end() eight times over and compares free heap, minimum free heap and
    the FreeRTOS task count between rounds.

    The assertion is drift, not an absolute number: the first round pays for one-time
    allocations that are never returned (controller buffers, NVS handles), so the
    baseline is the second. What this catches is the failure that a connect-once suite
    cannot — a few hundred bytes or one task per connection, invisible here and fatal
    in a sketch that reconnects all day.
    """
    peer = peers["device"]
    # Both boards are asked to report rather than waited on. They are flashed one
    # after the other, so whatever the first one printed while the second was being
    # flashed is already gone, and pytest resets a board only when its binary changed
    # (tests/TEST_PLAN.md).
    dut.write("?")
    dut.expect(
        re.compile(rb"STATE running=0 cycle=0 stage=idle heap=(\d+) tasks=(\d+)"),
        timeout=30,
    )
    peer.write("?")
    before = peer.expect(
        re.compile(rb"PEER_STATE connects=(\d+) disconnects=(\d+) writes=(\d+) "
                   rb"heap=\d+"),
        timeout=30,
    )
    baseline_connects = int(before.group(1))
    baseline_writes = int(before.group(3))

    dut.write("g")
    cycles = []
    for index in range(CYCLES):
        match = dut.expect(CYCLE, timeout=60)
        number = int(match.group(1))
        assert number == index + 1, "rounds must be reported in order"
        assert match.group(2) == b"1", (
            "round %d stopped at stage %s" % (number, match.group(4).decode())
        )
        assert match.group(3) == b"1", "end() must succeed after every round"
        assert match.group(4) == b"disconnect", (
            "a completed round ends at the disconnect: %s" % match.group(4).decode()
        )
        assert int(match.group(5)) == LOCAL_HOST_TERMINATED, (
            "round %d ended with HCI reason %s, so the link went away rather than "
            "being closed by the sketch" % (number, match.group(5).decode())
        )
        assert match.group(9) == b"0", "no event may be dropped"
        assert match.group(10) == b"loop", "the teardown runs in the sketch's context"
        cycles.append(
            {
                "heap": int(match.group(6)),
                "minheap": int(match.group(7)),
                "tasks": int(match.group(8)),
            }
        )
    dut.expect_exact("CYCLES_DONE count=%d" % CYCLES, timeout=20)

    baseline = cycles[1]
    last = cycles[-1]
    drift = baseline["heap"] - last["heap"]
    assert drift <= MAX_HEAP_DRIFT, (
        "free heap fell %d bytes between round 2 (%d) and round %d (%d); a "
        "per-lifecycle leak looks exactly like this. Every round: %s"
        % (drift, baseline["heap"], CYCLES, last["heap"],
           [entry["heap"] for entry in cycles])
    )
    # A leak does not have to be monotonic to be real, but a monotonic fall over six
    # rounds is not fragmentation.
    falling = all(
        cycles[index]["heap"] >= cycles[index + 1]["heap"]
        for index in range(1, len(cycles) - 1)
    )
    assert not (falling and baseline["heap"] > last["heap"]), (
        "free heap fell in every round from the second on: %s"
        % [entry["heap"] for entry in cycles]
    )
    assert last["tasks"] == baseline["tasks"], (
        "the task count must return to the same value after end(): %s"
        % [entry["tasks"] for entry in cycles]
    )

    # The peer saw every round, which is what makes the heap numbers meaningful:
    # eight teardowns of a link that really was up.
    peer.write("?")
    peer_state = peer.expect(
        re.compile(rb"PEER_STATE connects=(\d+) disconnects=(\d+) writes=(\d+) "
                   rb"heap=(\d+)"),
        timeout=25,
    )
    # Counted from the baseline read at the start: the peer keeps counting across a
    # run in which pytest did not reflash it, so an absolute count would be wrong the
    # second time this suite runs.
    assert int(peer_state.group(1)) - baseline_connects == CYCLES, (
        "the peer accepted %d connections, not %d"
        % (int(peer_state.group(1)) - baseline_connects, CYCLES)
    )
    assert int(peer_state.group(3)) - baseline_writes == CYCLES, (
        "one write per round reached the peer"
    )

    dut.write("?")
    dut.expect(re.compile(rb"STATE running=0 cycle=%d stage=\w+" % CYCLES), timeout=25)
