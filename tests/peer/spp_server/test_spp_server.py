import re


def test_public_spp_server_bidirectional_data_and_disconnect(dut, peers):
    client = peers["device"]
    client.write("i")
    client.expect_exact("SPP_RAW_CLIENT_READY", timeout=30)

    dut.write("i")
    dut.expect_exact("SPP_CAPABILITIES classic=1 spp=1", timeout=30)
    dut.expect_exact("SPP_PREBEGIN_REJECTED 1 error=InvalidState", timeout=30)
    dut.expect_exact("SPP_SERVER_START_ACCEPTED 1", timeout=30)
    started = dut.expect(
        re.compile(
            rb"SPP_SERVER_STARTED address=([0-9a-f:]{17}) "
            rb"running=1 context=loop"
        ),
        timeout=30,
    )
    address = started.group(1).decode().replace(":", "")
    client.write(f"c{address}\n")

    session_ids = []
    for attempt in range(2):
        if attempt:
            client.write(f"c{address}\n")
        client.expect_exact("SPP_RAW_CONNECTED", timeout=30)
        connected = dut.expect(
            re.compile(
                rb"SPP_SERVER_CONNECTED id=(\d+) "
                rb"address=([0-9a-f:]{17}) incoming=1 context=loop"
            ),
            timeout=30,
        )
        session_id = int(connected.group(1))
        assert session_id != 0
        session_ids.append(session_id)
        dut.expect(
            re.compile(
                rb"SPP_SERVER_RX id=(\d+) length=3 hex=007f50 "
                rb"context=loop"
            ),
            timeout=30,
        )
        dut.expect_exact(
            "SPP_SERVER_WRITE_ACCEPTED 111111110 "
            f"pending=8 dropped={attempt + 1}",
            timeout=30,
        )
        for _ in range(8):
            dut.expect_exact(
                f"SPP_SERVER_WRITE_COMPLETED id={session_id} length=3 "
                "success=1 error=0 context=loop",
                timeout=30,
            )
        client.expect_exact(
            "SPP_RAW_RX length=24 "
            "hex=a00053a10053a20053a30053"
            "a40053a50053a60053a70053",
            timeout=30,
        )
        client.expect_exact("SPP_RAW_DISCONNECTED", timeout=30)
        dut.expect(
            re.compile(
                rb"SPP_SERVER_DISCONNECTED id=(\d+) "
                rb"remaining=0 context=loop"
            ),
            timeout=30,
        )

    assert session_ids[1] != session_ids[0]
    dut.write("e")
    ended = dut.expect(
        re.compile(rb"SPP_END_DONE initialized=0 elapsed=(\d+)"),
        timeout=30,
    )
    assert int(ended.group(1)) <= 1500
