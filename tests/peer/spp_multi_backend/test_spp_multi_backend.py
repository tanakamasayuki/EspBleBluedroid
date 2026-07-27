import re


def test_raw_bluedroid_supports_two_spp_sessions_on_one_peer(dut, peers):
    client = peers["device"]
    client.write("i")
    client.expect_exact("SPP_MULTI_CLIENT_READY", timeout=30)

    dut.write("i")
    ready = dut.expect(
        re.compile(
            rb"SPP_MULTI_SERVER_READY address=([0-9a-f:]{17}) "
            rb"acl_max=(\d+) channels=(\d+),(\d+)"
        ),
        timeout=30,
    )
    assert int(ready.group(2)) >= 1
    assert int(ready.group(3)) != int(ready.group(4))
    address = ready.group(1).decode().replace(":", "")
    client.write(f"c{address}\n")

    client_slots = set()
    server_slots = set()
    for expected_count in (1, 2):
        client_connected = client.expect(
            re.compile(
                rb"SPP_MULTI_CLIENT_CONNECTED slot=(\d+) "
                rb"handle=(\d+) count=(\d+)"
            ),
            timeout=30,
        )
        client_slots.add(int(client_connected.group(1)))
        assert int(client_connected.group(2)) != 0
        assert int(client_connected.group(3)) == expected_count

        server_connected = dut.expect(
            re.compile(
                rb"SPP_MULTI_SERVER_CONNECTED slot=(\d+) "
                rb"handle=(\d+) count=(\d+)"
            ),
            timeout=30,
        )
        server_slots.add(int(server_connected.group(1)))
        assert int(server_connected.group(2)) != 0
        assert int(server_connected.group(3)) == expected_count

    assert client_slots == {1, 2}
    assert server_slots == {1, 2}

    server_markers = set()
    client_markers = set()
    for _ in range(2):
        server_data = dut.expect(
            re.compile(
                rb"SPP_MULTI_SERVER_RX slot=(\d+) marker=(\d+) length=2"
            ),
            timeout=30,
        )
        server_markers.add(int(server_data.group(2)))

        client_data = client.expect(
            re.compile(
                rb"SPP_MULTI_CLIENT_RX slot=(\d+) marker=(\d+) length=2"
            ),
            timeout=30,
        )
        client_markers.add(int(client_data.group(2)))

    assert server_markers == {1, 2}
    assert client_markers == {1, 2}
    client.expect_exact("SPP_MULTI_CLIENT_BOTH_RESPONSES", timeout=30)

    client_closed = set()
    server_closed = set()
    for _ in range(2):
        client_close = client.expect(
            re.compile(
                rb"SPP_MULTI_CLIENT_DISCONNECTED slot=(\d+) count=(\d+)"
            ),
            timeout=30,
        )
        client_closed.add(int(client_close.group(1)))

        server_close = dut.expect(
            re.compile(
                rb"SPP_MULTI_SERVER_DISCONNECTED slot=(\d+) count=(\d+)"
            ),
            timeout=30,
        )
        server_closed.add(int(server_close.group(1)))

    assert client_closed == {1, 2}
    assert server_closed == {1, 2}
    client.expect_exact("SPP_MULTI_CLIENT_IDLE count=0", timeout=30)
    dut.expect_exact("SPP_MULTI_SERVER_IDLE count=0", timeout=30)
