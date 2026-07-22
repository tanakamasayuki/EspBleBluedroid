import re


def test_connection_id_disconnect_and_deferred_callbacks(dut, peers):
    peripheral = peers["device"]
    dut.expect_exact("INVALID_ADDRESS_REJECTED 1 error=InvalidArgument", timeout=20)
    dut.expect_exact("CENTRAL_READY", timeout=20)
    peripheral.expect_exact("PEER_READY", timeout=20)

    dut.write("s")
    dut.expect_exact("SCAN_STARTED 1", timeout=20)
    dut.expect_exact("CONNECT_ACCEPTED 1", timeout=30)
    dut.expect_exact("CONCURRENT_REJECTED 1 error=InvalidState", timeout=20)
    dut.expect_exact("UPDATE_PAUSED", timeout=20)
    peripheral.expect_exact("PEER_CONNECTED", timeout=20)

    # The radio link is already up, but the public callback must remain queued
    # until update() is enabled again.
    dut.write("u")
    dut.expect_exact("UPDATE_START", timeout=20)
    connected = dut.expect(
        re.compile(rb"CONNECTED seq=1 id=(\d+) stable=1 fresh=1 mtu=(\d+)"),
        timeout=20,
    )
    connection_id = int(connected.group(1))
    assert connection_id > 0
    assert int(connected.group(2)) >= 23
    dut.expect_exact("DISCONNECT_ACCEPTED 1", timeout=20)
    disconnected = dut.expect(
        re.compile(rb"DISCONNECTED seq=1 id=(\d+) count=0"), timeout=20
    )
    assert int(disconnected.group(1)) == connection_id
    peripheral.expect_exact("PEER_DISCONNECTED", timeout=20)
    peripheral.expect_exact("PEER_READVERTISING", timeout=20)
    dut.expect_exact("RECONNECT_COMMAND_READY", timeout=20)

    dut.write("r")
    dut.expect_exact("RECONNECT_SCAN_STARTED 1", timeout=20)
    dut.expect_exact("CONNECT_ACCEPTED 1", timeout=30)
    dut.expect_exact("CONCURRENT_REJECTED 1 error=InvalidState", timeout=20)
    dut.expect_exact("UPDATE_PAUSED", timeout=20)
    peripheral.expect_exact("PEER_CONNECTED", timeout=20)
    dut.write("u")
    dut.expect_exact("UPDATE_START", timeout=20)
    reconnected = dut.expect(
        re.compile(rb"CONNECTED seq=2 id=(\d+) stable=1 fresh=1 mtu=(\d+)"),
        timeout=20,
    )
    second_id = int(reconnected.group(1))
    assert second_id != connection_id
    dut.expect_exact("DISCONNECT_ACCEPTED 1", timeout=20)
    second_disconnected = dut.expect(
        re.compile(rb"DISCONNECTED seq=2 id=(\d+) count=0"), timeout=20
    )
    assert int(second_disconnected.group(1)) == second_id
    peripheral.expect_exact("PEER_DISCONNECTED", timeout=20)
    dut.expect_exact("FAILURE_REQUEST_ACCEPTED 1", timeout=20)
    failure = dut.expect(
        re.compile(
            rb"CONNECTION_FAILED address=02:00:00:00:00:01 "
            rb"error=(\d+) detail=BLE connection failed"
        ),
        timeout=15,
    )
    # Depending on how quickly Bluedroid rejects the direct request, this is
    # BackendFailure or Timeout; both are explicit asynchronous failures.
    assert int(failure.group(1)) in {3, 6}
    dut.expect_exact("REINITIALIZED 1", timeout=20)
