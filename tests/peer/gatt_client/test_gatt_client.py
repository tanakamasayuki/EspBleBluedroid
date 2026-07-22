def test_characteristic_read_write_is_binary_safe_and_deferred(dut, peers):
    peripheral = peers["device"]
    dut.expect_exact("INVALID_READ_REJECTED 1 error=InvalidArgument", timeout=20)
    dut.expect_exact("INVALID_WRITE_REJECTED 1 error=InvalidArgument", timeout=20)
    dut.expect_exact(
        "INVALID_DESCRIPTOR_READ_REJECTED 1 error=InvalidArgument", timeout=20
    )
    dut.expect_exact("ZERO_HANDLE_REJECTED 1 error=InvalidArgument", timeout=20)
    dut.expect_exact("GATT_CENTRAL_READY", timeout=20)
    peripheral.expect_exact("GATT_PEER_READY", timeout=20)

    dut.write("s")
    dut.expect_exact("SCAN_STARTED 1", timeout=20)
    dut.expect_exact("CONNECT_REQUESTED 1", timeout=30)
    peripheral.expect_exact("GATT_PEER_CONNECTED", timeout=20)
    dut.expect_exact("DISCOVERY_REQUESTED 1", timeout=20)
    dut.expect_exact("CONCURRENT_GATT_REJECTED 1 error=InvalidState", timeout=20)
    dut.expect_exact("GATT_UPDATE_PAUSED", timeout=20)

    dut.write("u")
    dut.expect_exact("GATT_UPDATE_START", timeout=20)
    discovery = dut.expect(
        rb"DISCOVERY success=1 services=(\d+) found=1 cccd=1 custom_descriptor=1 "
        rb"properties=1 context=loop",
        timeout=30,
    )
    assert int(discovery.group(1)) >= 1
    dut.expect_exact("MISSING_HANDLE_READ_REQUESTED 1", timeout=20)
    dut.expect_exact(
        "MISSING_HANDLE_READ_RESULT success=0 error=NotFound handle=65534 context=loop",
        timeout=20,
    )
    dut.expect_exact("DESCRIPTOR_READ_REQUESTED 1", timeout=20)
    dut.expect_exact(
        "DESCRIPTOR_READ_RESULT success=1 length=4 hex=4400ff53 context=loop",
        timeout=20,
    )
    dut.expect_exact("DESCRIPTOR_WRITE_REQUESTED 1", timeout=20)
    peripheral.expect_exact(
        "GATT_PEER_DESCRIPTOR_WRITE length=3 hex=0044fe", timeout=20
    )
    dut.expect_exact(
        "DESCRIPTOR_WRITE_RESULT success=1 response=1 length=3 context=loop",
        timeout=20,
    )
    handle_read = dut.expect(rb"HANDLE_READ_REQUESTED 1 handle=(\d+)", timeout=20)
    assert int(handle_read.group(1)) > 0
    dut.expect_exact(
        "HANDLE_READ_RESULT success=1 length=4 hex=0041ff42 context=loop",
        timeout=20,
    )
    dut.expect_exact("HANDLE_WRITE_RESPONSE_REQUESTED 1", timeout=20)
    peripheral.expect_exact("GATT_PEER_WRITE length=3 hex=0057ff", timeout=20)
    dut.expect_exact(
        "WRITE_RESULT phase=0 success=1 response=1 length=3 context=loop",
        timeout=20,
    )
    dut.expect_exact("HANDLE_WRITE_NO_RESPONSE_REQUESTED 1", timeout=20)
    peripheral.expect_exact("GATT_PEER_WRITE length=3 hex=4e0052", timeout=20)
    dut.expect_exact(
        "WRITE_RESULT phase=1 success=1 response=0 length=3 context=loop",
        timeout=20,
    )
    dut.expect_exact("HANDLE_SUBSCRIBE_REQUESTED 1", timeout=20)
    dut.expect_exact(
        "SUBSCRIBED success=1 notifications=1 context=loop", timeout=20
    )
    peripheral.write("n")
    peripheral.expect_exact("GATT_PEER_NOTIFIED", timeout=20)
    dut.expect_exact("NOTIFICATION valid=1 indication=0 context=loop", timeout=20)
    dut.expect_exact("HANDLE_UNSUBSCRIBE_REQUESTED 1", timeout=20)
    dut.expect_exact("UNSUBSCRIBED success=1 context=loop", timeout=20)
    dut.expect_exact("GATT_DISCONNECTED snapshot_services=0 context=loop", timeout=20)
