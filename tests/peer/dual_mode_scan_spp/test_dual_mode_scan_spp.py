import re


def test_ble_gatt_and_spp_data_share_dual_mode_stack(dut, peers):
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
    dut.expect_exact("DUAL_BLE_CONNECT_ACCEPTED 1", timeout=30)
    ble_connected = dut.expect(
        re.compile(
            rb"DUAL_BLE_CONNECTED id=(\d+) spp_sessions=1 context=loop"
        ),
        timeout=30,
    )
    ble_connection_id = int(ble_connected.group(1))
    assert ble_connection_id != 0
    dut.expect_exact("DUAL_SPP_WRITE_ACCEPTED 1", timeout=30)
    peer.expect_exact("DUAL_PEER_SPP_RX length=3 hex=d00048", timeout=30)
    dut.expect_exact(
        f"DUAL_SPP_RX id={session_id} length=3 hex=d10050 phase=1 "
        "ble_connections=1 context=loop",
        timeout=30,
    )
    dut.expect_exact("DUAL_GATT_DISCOVERY_ACCEPTED 1", timeout=30)
    discovered = dut.expect(
        re.compile(
            rb"DUAL_GATT_DISCOVERED success=1 found=1 handle=(\d+) "
            rb"spp_sessions=1 context=loop"
        ),
        timeout=30,
    )
    assert int(discovered.group(1)) > 0
    dut.expect_exact("DUAL_GATT_READ_ACCEPTED 1", timeout=30)
    dut.expect_exact(
        "DUAL_GATT_READ valid=1 spp_sessions=1 context=loop",
        timeout=30,
    )
    dut.expect_exact("DUAL_GATT_WRITE_ACCEPTED 1", timeout=30)
    peer.expect_exact("DUAL_PEER_GATT_WRITE length=3 hex=b10057", timeout=30)
    dut.expect_exact(
        "DUAL_GATT_WRITTEN success=1 spp_sessions=1 context=loop",
        timeout=30,
    )
    dut.expect_exact("DUAL_GATT_SUBSCRIBE_ACCEPTED 1", timeout=30)
    dut.expect_exact(
        "DUAL_GATT_SUBSCRIBED success=1 spp_sessions=1 context=loop",
        timeout=30,
    )

    for cycle in range(16):
        peer.write("n")
        peer.expect_exact("DUAL_PEER_GATT_NOTIFIED", timeout=30)
        dut.expect_exact(
            "DUAL_GATT_NOTIFICATION valid=1 spp_sessions=1 context=loop",
            timeout=30,
        )
        dut.expect_exact(
            "DUAL_SPP_DURING_GATT_WRITE_ACCEPTED 1", timeout=30
        )
        peer.expect_exact(
            "DUAL_PEER_SPP_RX length=3 hex=d20047", timeout=30
        )
        dut.expect_exact(
            f"DUAL_SPP_RX id={session_id} length=3 hex=d10050 "
            f"phase={cycle + 2} ble_connections=1 context=loop",
            timeout=30,
        )
    dut.expect_exact("DUAL_GATT_UNSUBSCRIBE_ACCEPTED 1", timeout=30)
    dut.expect_exact(
        "DUAL_GATT_UNSUBSCRIBED success=1 spp_sessions=1 context=loop",
        timeout=30,
    )
    dut.expect_exact("DUAL_BLE_DISCONNECT_ACCEPTED 1", timeout=30)
    dut.expect_exact("DUAL_SPP_DISCONNECT_ACCEPTED 1", timeout=30)
    peer.expect_exact("DUAL_PEER_SPP_DISCONNECTED", timeout=30)
    dut.expect(
        re.compile(
            rb"DUAL_BLE_DISCONNECTED id="
            + str(ble_connection_id).encode()
            + rb" ble_connections=0 context=loop"
        ),
        timeout=30,
    )
    dut.expect_exact(
        f"DUAL_SPP_DISCONNECTED id={session_id} sessions=0 context=loop",
        timeout=30,
    )
