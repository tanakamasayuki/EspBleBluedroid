def test_characteristic_read_is_binary_safe_and_deferred(dut, peers):
    peripheral = peers["device"]
    dut.expect_exact("INVALID_READ_REJECTED 1 error=InvalidArgument", timeout=20)
    dut.expect_exact("GATT_CENTRAL_READY", timeout=20)
    peripheral.expect_exact("GATT_PEER_READY", timeout=20)

    dut.write("s")
    dut.expect_exact("SCAN_STARTED 1", timeout=20)
    dut.expect_exact("CONNECT_REQUESTED 1", timeout=30)
    peripheral.expect_exact("GATT_PEER_CONNECTED", timeout=20)
    dut.expect_exact("READ_REQUESTED 1", timeout=20)
    dut.expect_exact("GATT_UPDATE_PAUSED", timeout=20)

    dut.write("u")
    dut.expect_exact("GATT_UPDATE_START", timeout=20)
    dut.expect_exact(
        "READ_RESULT success=1 length=4 hex=0041ff42 context=loop", timeout=20
    )
