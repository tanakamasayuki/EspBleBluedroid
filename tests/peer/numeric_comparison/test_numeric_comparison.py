import re


def test_numeric_comparison(dut, peers):
    peripheral = peers["device"]
    dut.expect_exact("NUMCMP_CENTRAL_READY", timeout=20)
    peripheral.expect_exact("NUMCMP_PEER_READY", timeout=20)
    dut.write("x")
    peripheral.write("x")
    dut.expect_exact(
        "NUMCMP_CENTRAL_BONDS_CLEARED success=1 count=0", timeout=20
    )
    peripheral.expect_exact(
        "NUMCMP_PEER_BONDS_CLEARED success=1 count=0", timeout=20
    )

    dut.write("s")
    dut.expect_exact("NUMCMP_SCAN_STARTED 1", timeout=20)
    dut.expect_exact("NUMCMP_CONNECT_REQUESTED 1", timeout=30)
    peripheral.expect_exact("NUMCMP_PEER_CONNECTED", timeout=20)
    dut.expect_exact("NUMCMP_CENTRAL_CONNECTED id=1", timeout=20)
    central_value = dut.expect(
        re.compile(rb"NUMCMP_CENTRAL_VALUE id=1 value=(\d{6}) context=loop"),
        timeout=30,
    ).group(1)
    peripheral_value = peripheral.expect(
        re.compile(rb"NUMCMP_PEER_VALUE value=(\d{6})"), timeout=30
    ).group(1)
    assert central_value == peripheral_value

    dut.write("y")
    peripheral.write("y")
    dut.expect_exact("NUMCMP_CENTRAL_CONFIRM accepted=1", timeout=20)
    peripheral.expect_exact("NUMCMP_PEER_CONFIRM accepted=1", timeout=20)
    dut.expect_exact(
        "NUMCMP_CENTRAL_SECURITY success=1 encrypted=1 authenticated=1 bonded=1 key=16 context=loop",
        timeout=30,
    )
    peripheral.expect_exact(
        "NUMCMP_PEER_SECURITY success=1 encrypted=1 authenticated=1 bonded=1 key=16",
        timeout=30,
    )

    dut.write("d")
    dut.expect_exact("NUMCMP_DISCONNECT_REQUESTED 1", timeout=20)
    dut.expect_exact(
        "NUMCMP_CENTRAL_DISCONNECTED id=1 context=loop", timeout=20
    )
    peripheral.expect_exact(
        "NUMCMP_PEER_DISCONNECTED authenticated=1", timeout=20
    )
    dut.write("x")
    peripheral.write("x")
    dut.expect_exact(
        "NUMCMP_CENTRAL_BONDS_CLEARED success=1 count=0", timeout=20
    )
    peripheral.expect_exact(
        "NUMCMP_PEER_BONDS_CLEARED success=1 count=0", timeout=20
    )
