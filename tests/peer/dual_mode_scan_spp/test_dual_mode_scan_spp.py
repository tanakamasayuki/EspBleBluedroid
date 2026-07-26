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
    dut.write("p")
    dut.expect_exact(
        "DUAL_PRIORITY_INJECTED notifications=8 control=1", timeout=30
    )
    dut.expect_exact(
        "DUAL_PRIORITY_COMPLETE notifications=7 dropped=1 "
        "success=1 context=loop",
        timeout=30,
    )
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

    for round_index in range(3):
        peer.write("t64\n")
        peer.expect_exact("DUAL_PEER_TRAFFIC_STARTED count=64", timeout=30)
        peer.expect_exact("DUAL_PEER_TRAFFIC_SENT", timeout=30)
        peer_responses = peer.expect(
            re.compile(
                rb"DUAL_PEER_RESPONSES_IDLE received=(\d+) completed=(\d+)"
            ),
            timeout=30,
        )
        peer_received = int(peer_responses.group(1))
        assert peer_received == int(peer_responses.group(2))
        assert 0 < peer_received <= 64

        final_round = round_index == 2
        dut.write("q" if final_round else "r")
        traffic = dut.expect(
            re.compile(
                rb"DUAL_TRAFFIC_(?:COMPLETE|ROUND) round=(\d+) "
                rb"notifications=(\d+) ring_packets=(\d+) "
                rb"ring_bytes=(\d+) spp_callbacks=(\d+) "
                rb"ble_event_dropped=(\d+) spp_event_dropped=(\d+) "
                rb"spp_rx_dropped=0 spp_write_dropped=0 app_pending=0"
            ),
            timeout=30,
        )
        assert int(traffic.group(1)) == round_index + 1
        notification_count = int(traffic.group(2))
        ring_packet_count = int(traffic.group(3))
        ring_byte_count = int(traffic.group(4))
        callback_count = int(traffic.group(5))
        dropped_ble_event_count = int(traffic.group(6))
        dropped_spp_event_count = int(traffic.group(7))
        assert notification_count + dropped_ble_event_count == 64
        assert notification_count == peer_received
        assert ring_packet_count == peer_received
        assert ring_byte_count == peer_received * 3
        assert callback_count > 0
        assert callback_count + dropped_spp_event_count <= peer_received
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
