def test_ibeacon_broadcast_and_decode(dut, peers):
    peripheral = peers["device"]

    peripheral.write("?")
    peripheral.expect_exact("ADVERTISING 1", timeout=10)
    dut.write("s")
    dut.expect_exact("SCAN_STARTED", timeout=10)
    dut.expect_exact(
        "IBEACON uuid=0102030405060708090a0b0c0d0e0f10 "
        "major=4660 minor=43981 power=-59 connectable=0 scannable=0",
        timeout=20,
    )
