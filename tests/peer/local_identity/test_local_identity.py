import re


OBSERVED = re.compile(rb"OBSERVED address=(\S+) type=(\d+) txpower=(\S+)")
LOCAL = re.compile(rb"LOCAL address=(\S+) type=(\d+)")


def observe(dut):
    dut.write("s")
    dut.expect_exact("SCAN_STARTED", timeout=10)
    match = dut.expect(OBSERVED, timeout=20)
    return (
        match.group(1).decode().lower(),
        int(match.group(2)),
        match.group(3).decode(),
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
    peripheral.expect_exact("RPA_READY 1 error=None", timeout=20)
    peripheral.write("a")
    local = peripheral.expect(LOCAL, timeout=10)
    rpa_local_address = local.group(1).decode().lower()
    assert rpa_local_address == "-"
    assert int(local.group(2)) == 1

    rpa_observed_address, rpa_observed_type, _ = observe(dut)
    assert rpa_observed_type == 1
    rpa_most_significant_octet = int(rpa_observed_address.split(":")[0], 16)
    assert (rpa_most_significant_octet & 0xC0) == 0x40
