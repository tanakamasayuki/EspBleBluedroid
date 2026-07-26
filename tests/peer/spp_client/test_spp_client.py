import re


def test_public_spp_client_uses_shared_session_api(dut, peers):
    server = peers["device"]
    server.write("i")
    ready = server.expect(
        re.compile(rb"SPP_RAW_SERVER_READY address=([0-9a-f:]{17})"),
        timeout=30,
    )
    address = ready.group(1).decode()

    dut.write("i")
    dut.expect_exact(
        "SPP_CLIENT_PREBEGIN_REJECTED 1 error=InvalidState", timeout=30
    )
    dut.expect_exact("SPP_PUBLIC_CLIENT_READY", timeout=30)

    session_ids = []
    for _ in range(2):
        dut.write(f"c{address}\n")
        accepted = dut.expect(
            re.compile(rb"SPP_CLIENT_CONNECT_ACCEPTED 1 elapsed=(\d+)"),
            timeout=30,
        )
        assert int(accepted.group(1)) < 100
        server.expect_exact("SPP_RAW_SERVER_CONNECTED", timeout=30)
        connected = dut.expect(
            re.compile(
                rb"SPP_CLIENT_CONNECTED id=(\d+) "
                rb"address=([0-9a-f:]{17}) incoming=0 "
                rb"stream=1 stream_id=(\d+) context=loop"
            ),
            timeout=30,
        )
        session_id = int(connected.group(1))
        assert int(connected.group(3)) == session_id
        session_ids.append(session_id)
        dut.expect_exact("SPP_CLIENT_WRITE_ACCEPTED 1", timeout=30)
        dut.expect_exact(
            f"SPP_CLIENT_WRITE_COMPLETED id={session_ids[-1]} length=3 "
            "success=1 error=0 context=loop",
            timeout=30,
        )
        dut.expect(
            re.compile(
                rb"SPP_CLIENT_RX id=(\d+) length=3 hex=010052 "
                rb"context=loop"
            ),
            timeout=30,
        )
        server.expect_exact(
            "SPP_RAW_SERVER_RX length=3 hex=fe0043", timeout=30
        )
        dut.write("d")
        dut.expect_exact("SPP_CLIENT_DISCONNECT_ACCEPTED 1", timeout=30)
        server.expect_exact("SPP_RAW_SERVER_DISCONNECTED", timeout=30)
        dut.expect(
            re.compile(
                rb"SPP_CLIENT_DISCONNECTED id=(\d+) remaining=0 "
                rb"stream=0 stream_id=0 context=loop"
            ),
            timeout=30,
        )

    assert session_ids[0] != 0
    assert session_ids[1] != session_ids[0]

    server.write("x")
    server.expect_exact("SPP_RAW_SERVER_STOPPED", timeout=30)
    dut.write(f"f{address}\n")
    failure_accepted = dut.expect(
        re.compile(rb"SPP_CLIENT_FAILURE_ACCEPTED 1 elapsed=(\d+)"),
        timeout=30,
    )
    assert int(failure_accepted.group(1)) < 100
    dut.expect(
        re.compile(
            rb"SPP_CLIENT_FAILED address="
            + address.encode()
            + rb" error=(5|6) context=loop"
        ),
        timeout=30,
    )
