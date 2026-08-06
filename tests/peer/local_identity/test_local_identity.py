import re


OBSERVED = re.compile(rb"OBSERVED address=(\S+) type=(\d+) txpower=(\S+)")
LOCAL = re.compile(rb"LOCAL address=(\S+) type=(\d+)")


def observe(dut, timeout=20):
    dut.write("s")
    dut.expect_exact("SCAN_STARTED", timeout=10)
    match = dut.expect(OBSERVED, timeout=timeout)
    return (
        match.group(1).decode().lower(),
        int(match.group(2)),
        match.group(3).decode(),
    )


def is_resolvable_private(address, address_type):
    """A Resolvable Private Address: random, with the top two bits 01."""
    return address_type == 1 and (int(address.split(":")[0], 16) & 0xC0) == 0x40


def observe_resolvable_private(dut, attempts=4):
    """Observe until the advertiser is using an RPA, reporting what came first.

    One sample is not enough here. The advertiser has just been through
    `end()` → `begin(ResolvablePrivate)` → `advertising.start()`, and the controller has
    been seen to put its identity address on air for a moment during that transition —
    only in long runs, and after suites that clear the bond store, never when this suite
    runs alone. Judging the phase by the first advertisement therefore fails on a
    transient rather than on the thing being tested, which is that the advertiser *ends
    up* using an RPA.

    What is not hidden: every non-RPA sample is returned and printed, and running out of
    attempts still fails. A controller that keeps advertising its identity address is a
    privacy leak and this still catches it.
    """
    rejected = []
    for _ in range(attempts):
        address, address_type, tx_power = observe(dut, timeout=10)
        if is_resolvable_private(address, address_type):
            return address, tx_power, rejected
        rejected.append((address, address_type))
    raise AssertionError(
        "the advertiser never used an RPA in %d observations: %s"
        % (attempts, rejected)
    )


def test_random_static_identity_and_tx_power(dut, peers):
    peripheral = peers["device"]

    dut.expect_exact("OBSERVER_READY", timeout=20)
    peripheral.expect_exact("IDENTITY_READY", timeout=20)
    peripheral.expect_exact(
        "PREBEGIN address_empty=1 tx_unknown=1", timeout=20
    )

    peripheral.write("a")
    local = peripheral.expect(LOCAL, timeout=10)
    local_address = local.group(1).decode().lower()
    local_type = int(local.group(2))
    assert local_type == 1

    observed_address, observed_type, _ = observe(dut)
    assert observed_address == local_address
    assert observed_type == local_type
    # A static random address has the top two bits 11, and does not rotate, so one
    # observation settles it.
    most_significant_octet = int(observed_address.split(":")[0], 16)
    assert (most_significant_octet & 0xC0) == 0xC0

    peripheral.write("l")
    peripheral.expect_exact(
        "POWER accepted=1 applied=-12 restarted=1", timeout=10
    )
    _, _, low_power = observe(dut)
    assert low_power == "-12"

    peripheral.write("h")
    peripheral.expect_exact(
        "POWER accepted=1 applied=9 restarted=1", timeout=10
    )
    _, _, high_power = observe(dut)
    assert high_power == "9"

    peripheral.write("r")
    peripheral.expect_exact("RPA_READY 1 error=NONE", timeout=20)
    peripheral.write("a")
    local = peripheral.expect(LOCAL, timeout=10)
    rpa_local_address = local.group(1).decode().lower()
    assert rpa_local_address == "-"
    assert int(local.group(2)) == 1

    rpa_observed_address, _, rejected = observe_resolvable_private(dut)
    # Printed rather than swallowed: the identity address appearing during the
    # transition is a real observation about this controller, recorded in
    # tests/TEST_PLAN.md, and the number of times it happens is how anyone would notice
    # it getting worse.
    if rejected:
        print("RPA transition showed %d non-RPA advertisement(s) first: %s"
              % (len(rejected), rejected))
    assert rpa_observed_address != local_address, (
        "the RPA must differ from the static random identity used before it"
    )
