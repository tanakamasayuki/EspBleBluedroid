import re


def test_connection_parameters(dut, peers):
    device = peers["device"]

    dut.expect_exact("BAD_RANGE_REJECTED 1 error=InvalidArgument", timeout=20)
    dut.expect_exact("UNKNOWN_ID_REJECTED 1 error=InvalidArgument", timeout=20)
    dut.expect_exact("CENTRAL_READY", timeout=20)
    device.expect_exact("PEER_READY", timeout=20)

    dut.write("s")
    dut.expect_exact("SCAN_STARTED", timeout=10)
    dut.expect_exact("CONNECT_REQUESTED", timeout=20)
    connected = dut.expect(
        re.compile(
            rb"CONNECTED id=(\d+) interval=(\d+) latency=(\d+) "
            rb"timeout=(\d+) context=(\w+)"
        ),
        timeout=20,
    )
    assert int(connected.group(1)) > 0
    assert int(connected.group(2)) > 0
    assert int(connected.group(4)) > 0
    assert connected.group(5) == b"loop"
    device.expect_exact("PEER_CONNECTED", timeout=20)

    dut.write("p")
    dut.expect_exact("UPDATE_REQUESTED", timeout=10)
    updated = dut.expect(
        re.compile(
            rb"PARAMS_UPDATED interval=(\d+) latency=(\d+) timeout=(\d+) "
            rb"stable=1 context=(\w+)"
        ),
        timeout=20,
    )
    assert int(updated.group(1)) == 80
    assert int(updated.group(2)) == 0
    assert int(updated.group(3)) == 200
    assert updated.group(4) == b"loop"

    peer_updated = device.expect(
        re.compile(
            rb"PEER_PARAMS_UPDATED status=0 interval=(\d+) "
            rb"latency=(\d+) timeout=(\d+)"
        ),
        timeout=20,
    )
    assert int(peer_updated.group(1)) == 80
    assert int(peer_updated.group(2)) == 0
    assert int(peer_updated.group(3)) == 200

    dut.write("d")
    dut.expect_exact("DISCONNECT_REQUESTED", timeout=10)
    dut.expect(re.compile(rb"DISCONNECTED id=(\d+) context=loop"), timeout=20)
    device.expect_exact("PEER_DISCONNECTED", timeout=20)
