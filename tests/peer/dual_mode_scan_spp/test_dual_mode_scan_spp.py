import re


def test_ble_scan_and_spp_data_share_dual_mode_stack(dut, peers):
    peer = peers["device"]
    peer.write("i")
    ready = peer.expect(
        re.compile(rb"DUAL_PEER_READY address=([0-9a-f:]{17})"),
        timeout=30,
    )
    address = ready.group(1).decode()

    dut.write("i")
    dut.expect_exact("DUAL_HOST_READY", timeout=30)
    dut.write(f"c{address}\n")
    dut.expect_exact("DUAL_CONNECT_ACCEPTED 1", timeout=30)
    peer.expect_exact("DUAL_PEER_SPP_CONNECTED", timeout=30)
    connected = dut.expect(
        re.compile(
            rb"DUAL_SPP_CONNECTED id=(\d+) scan_started=1 context=loop"
        ),
        timeout=30,
    )
    session_id = int(connected.group(1))
    assert session_id != 0
    dut.expect_exact(
        "DUAL_BLE_SCAN_FOUND name=Bluedroid Dual Peer "
        "spp_sessions=1 stopped=1 context=loop",
        timeout=30,
    )
    dut.expect_exact("DUAL_SPP_WRITE_ACCEPTED 1", timeout=30)
    peer.expect_exact("DUAL_PEER_SPP_RX length=3 hex=d00048", timeout=30)
    dut.expect_exact(
        f"DUAL_SPP_RX id={session_id} length=3 hex=d10050 "
        "scan=0 context=loop",
        timeout=30,
    )
    peer.expect_exact("DUAL_PEER_SPP_DISCONNECTED", timeout=30)
    dut.expect_exact(
        f"DUAL_COMPLETE id={session_id} sessions=0 context=loop",
        timeout=30,
    )
