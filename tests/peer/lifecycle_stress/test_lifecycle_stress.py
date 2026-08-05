import re

CYCLES = 8
# HCI "connection terminated by local host": the round ended because the sketch asked,
# which is what makes its heap numbers mean anything. A link that dropped on its own
# would leave a different reason and a half-finished teardown.
LOCAL_HOST_TERMINATED = 0x16

# What the stack-only rounds may drift between the second round and the last. Free heap
# moves by a few bytes between rounds even when nothing is retained, so this is noise,
# not an allowance.
MAX_STACK_DRIFT = 64
# What a connection currently retains and does not give back. This is a **known
# defect**, not a budget: `begin()`/`end()` and scanning return everything, and the
# wrapper's own equivalent of this round returns everything too, so the bytes are this
# library's (tests/TEST_PLAN.md, "Known defects").
#
# These are drifts over the six rounds between the baseline and the last, not per-round
# figures: ~165-200 bytes per connect round measured as 992-1236 in total, and ~440 per
# full round measured as ~2584. The bounds sit about a third above the worst measurement
# so the suite fails when the leak gets *worse* while the defect is open, and they are
# meant to come down to MAX_STACK_DRIFT once the connect path is fixed.
MAX_CONNECT_DRIFT = 1600
MAX_FULL_DRIFT = 3600

CYCLE = re.compile(
    rb"CYCLE (\d+) ok=(\d+) ended=(\d+) stage=(\w+) reason=(\d+) heap=(\d+) "
    rb"minheap=(\d+) tasks=(\d+) dropped=(\d+) context=(\w+) mode=(\w+) start=(\d+) "
    rb"begun=(\d+) conn=(\d+) disc=(\d+) pre=(\d+)"
)


def run_mode(dut, command, label, expect_connection):
    """Run eight rounds at one depth and return their reported numbers."""
    dut.write(command)
    rounds = []
    for index in range(CYCLES):
        match = dut.expect(CYCLE, timeout=90)
        number = int(match.group(1))
        assert number == index + 1, "rounds must be reported in order"
        assert match.group(2) == b"1", (
            "%s round %d stopped at stage %s"
            % (label, number, match.group(4).decode())
        )
        assert match.group(3) == b"1", "end() must leave initialized() false"
        assert match.group(11) == label.encode(), "the round reports its own depth"
        assert int(match.group(5)) == LOCAL_HOST_TERMINATED, (
            "%s round %d ended with HCI reason %s, so the link went away rather than "
            "being closed by the sketch" % (label, number, match.group(5).decode())
        )
        assert match.group(9) == b"0", "no event may be dropped"
        assert match.group(10) == b"loop", "the teardown runs in the sketch's context"
        if expect_connection:
            assert int(match.group(14)) != 0, (
                "%s round %d never reached a connection" % (label, number)
            )
        rounds.append({
            "heap": int(match.group(6)),
            "tasks": int(match.group(8)),
            "start": int(match.group(12)),
            "begun": int(match.group(13)),
        })
    dut.expect_exact("CYCLES_DONE count=%d mode=%s" % (CYCLES, label), timeout=20)
    return rounds


def drift(rounds):
    """Free heap lost between the second round and the last.

    The second is the baseline: the first pays for one-time allocations the stack never
    gives back.
    """
    return rounds[1]["heap"] - rounds[-1]["heap"]


def check(rounds, label, limit):
    lost = drift(rounds)
    assert lost <= limit, (
        "%s lost %d bytes of free heap between round 2 (%d) and round %d (%d), over the "
        "%d the plan records for it. Every round: %s"
        % (label, lost, rounds[1]["heap"], CYCLES, rounds[-1]["heap"], limit,
           [entry["heap"] for entry in rounds])
    )
    assert rounds[-1]["tasks"] == rounds[1]["tasks"], (
        "%s must return to the same task count after end(): %s"
        % (label, [entry["tasks"] for entry in rounds])
    )
    return lost


def test_repeated_lifecycle(dut, peers):
    """Eight full lifecycles at four depths: what repetition retains, and where.

    `begin()` → `end()` and `begin()` → scan → `end()` return everything, and that is
    asserted tightly. A round that connects does not: it retains a few hundred bytes,
    which this suite found and which `tests/TEST_PLAN.md` records as an open defect,
    together with the controls that attributed it to this library rather than to the
    Arduino BLE wrapper. The bounds for those two depths are therefore regression bounds
    rather than budgets — they exist so the leak cannot grow unnoticed while the defect
    is open.
    """
    peer = peers["device"]
    # Both boards are asked to report rather than waited on. They are flashed one after
    # the other, so whatever the first one printed while the second was being flashed is
    # already gone, and pytest resets a board only when its binary changed
    # (tests/TEST_PLAN.md).
    dut.write("?")
    dut.expect(
        re.compile(rb"STATE running=0 cycle=\d+ stage=\w+ heap=(\d+) tasks=(\d+)"),
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

    # Shallow first: a deeper round is the sum of the shallower ones, so the shallow
    # numbers are what make the deep ones readable.
    stack_only = run_mode(dut, "b", "beginend", expect_connection=False)
    check(stack_only, "begin()/end()", MAX_STACK_DRIFT)
    scanning = run_mode(dut, "n", "scan", expect_connection=False)
    check(scanning, "begin()/scan/end()", MAX_STACK_DRIFT)

    # The stack itself costs the same every round, whatever the round then does. A
    # begin() that grew would be a different defect from a connection that retains.
    for rounds, label in [(stack_only, "beginend"), (scanning, "scan")]:
        costs = [entry["start"] - entry["begun"] for entry in rounds[1:]]
        assert max(costs) - min(costs) < 4096, (
            "what begin() costs must not grow across rounds (%s): %s" % (label, costs)
        )

    connecting = run_mode(dut, "c", "connect", expect_connection=True)
    connect_lost = check(connecting, "begin()/connect/end()", MAX_CONNECT_DRIFT)
    full = run_mode(dut, "g", "full", expect_connection=True)
    full_lost = check(full, "the full round", MAX_FULL_DRIFT)

    # The defect's shape, not only its size: the connection is where the loss appears.
    assert connect_lost > drift(stack_only), (
        "a connecting round is expected to retain more than a stack-only one while the "
        "defect is open (connect %d, stack %d); if it no longer does, the fix landed and "
        "these bounds should come down" % (connect_lost, drift(stack_only))
    )

    # The peer saw every round of the two connecting modes, which is what makes their
    # heap numbers meaningful: sixteen teardowns of links that really were up. Counted
    # from the baseline, because pytest does not reflash — or reset — an unchanged board.
    peer.write("?")
    peer_state = peer.expect(
        re.compile(rb"PEER_STATE connects=(\d+) disconnects=(\d+) writes=(\d+) "
                   rb"heap=(\d+)"),
        timeout=25,
    )
    assert int(peer_state.group(1)) - baseline_connects == 2 * CYCLES, (
        "the peer accepted %d connections, not %d"
        % (int(peer_state.group(1)) - baseline_connects, 2 * CYCLES)
    )
    assert int(peer_state.group(3)) - baseline_writes == CYCLES, (
        "one write per full round reached the peer"
    )
