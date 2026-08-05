import re

CYCLE = re.compile(
    rb"CYCLE (\d+) ok=(\d+) ended=\d+ stage=\w+ reason=\d+ heap=(\d+) minheap=\d+ "
    rb"tasks=(\d+) dropped=\d+ context=\w+ mode=(\w+)"
)


def collect(dut, command, label):
    dut.write(command)
    heaps = []
    tasks = []
    for index in range(8):
        match = dut.expect(CYCLE, timeout=90)
        # Asserted, not collected: a round that failed instantly reports a heap that
        # does not move, which reads exactly like "nothing leaks".
        assert match.group(2) == b"1", (
            "%s round %d failed" % (label, index + 1)
        )
        heaps.append(int(match.group(3)))
        tasks.append(int(match.group(4)))
    dut.expect_exact("CYCLES_DONE count=8", timeout=20)
    deltas = [heaps[i + 1] - heaps[i] for i in range(1, len(heaps) - 1)]
    per = (heaps[1] - heaps[-1]) / 6.0
    print("BISECT %-9s heaps=%s deltas=%s per_round=%.0f tasks=%s"
          % (label, heaps, deltas, per, tasks))
    return per


def test_bisect(dut, peers):
    dut.write("?")
    dut.expect(re.compile(rb"STATE running=0 cycle=\d+ stage=\w+"), timeout=30)
    results = {}
    # raw runs last: it is the one mode that touches the wrapper directly.
    for command, label in [("c", "connect"), ("g", "full"), ("W", "rawconnect"),
                           ("w", "raw")]:
        results[label] = collect(dut, command, label)
    print("BISECT SUMMARY %s" % results)
