import re


def test_connection_id_disconnect_and_deferred_callbacks(dut, peers):
    peripheral = peers["device"]
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
        re.compile(rb"CONNECTED id=(\d+) stable=1 mtu=(\d+)"), timeout=20
    )
    connection_id = int(connected.group(1))
    assert connection_id > 0
    assert int(connected.group(2)) >= 23
    dut.expect_exact("DISCONNECT_ACCEPTED 1", timeout=20)
    disconnected = dut.expect(
        re.compile(rb"DISCONNECTED id=(\d+) count=0"), timeout=20
    )
    assert int(disconnected.group(1)) == connection_id
    peripheral.expect_exact("PEER_DISCONNECTED", timeout=20)
    dut.expect_exact("REINITIALIZED 1", timeout=20)
