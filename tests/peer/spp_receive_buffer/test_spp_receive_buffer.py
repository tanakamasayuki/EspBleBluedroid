import re


def test_spp_receive_ring_is_binary_safe_and_bounded(dut, peers):
    client = peers["device"]
    client.write("i")
    client.expect_exact("SPP_RX_RAW_READY", timeout=30)

    dut.write("i")
    ready = dut.expect(
        re.compile(
            rb"SPP_RX_SERVER_READY address=([0-9a-f:]{17}) capacity=2048"
        ),
        timeout=30,
    )
    address = ready.group(1).decode().replace(":", "")
    client.write(f"c{address}\n")
    client.expect_exact("SPP_RX_RAW_CONNECTED", timeout=30)

    expected_checksum = sum(index % 251 for index in range(2048))
    buffered = dut.expect(
        re.compile(
            rb"SPP_RX_BUFFER event_bytes=2300 available=2048 dropped=252 "
            rb"peek=0 read=2048 remaining=0 checksum=(\d+) context=loop"
        ),
        timeout=30,
    )
    assert int(buffered.group(1)) == expected_checksum
    dut.expect_exact("SPP_RX_EMPTY_READ -1", timeout=30)
    client.expect_exact("SPP_RX_RAW_REPLY value=done sent=2300", timeout=30)
    client.expect_exact("SPP_RX_RAW_DISCONNECTED", timeout=30)
    dut.expect_exact("SPP_RX_DISCONNECTED available=0", timeout=30)
