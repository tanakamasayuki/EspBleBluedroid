import re


def test_spp_stream_wraps_session_as_arduino_stream(dut, peers):
    client = peers["device"]
    client.write("i")
    client.expect_exact("SPP_STREAM_RAW_READY", timeout=30)

    dut.write("i")
    dut.expect_exact("SPP_STREAM_DEFAULT connected=0 id=0", timeout=30)
    ready = dut.expect(
        re.compile(rb"SPP_STREAM_SERVER_READY address=([0-9a-f:]{17})"),
        timeout=30,
    )
    address = ready.group(1).decode().replace(":", "")
    client.write(f"c{address}\n")
    client.expect_exact("SPP_STREAM_RAW_CONNECTED", timeout=30)
    attached = dut.expect(
        re.compile(
            rb"SPP_STREAM_ATTACHED invalid=0 attached=1 connected=1 "
            rb"id=(\d+) stream=1 constructed=1 writable=7920"
        ),
        timeout=30,
    )
    assert int(attached.group(1)) != 0

    dut.expect_exact(
        "SPP_STREAM_IO first=0 single=0 remaining=410a "
        "available=0 printed=10 binary=1000",
        timeout=30,
    )
    expected_checksum = sum(index % 251 for index in range(1000))
    received = client.expect(
        re.compile(
            rb"SPP_STREAM_RAW_RX length=1010 prefix=1 binary=1 "
            rb"checksum=(\d+)"
        ),
        timeout=30,
    )
    assert int(received.group(1)) == expected_checksum
    client.expect_exact("SPP_STREAM_RAW_DISCONNECTED", timeout=30)
    dut.expect_exact(
        "SPP_STREAM_DISCONNECTED connected=0 available=0 "
        "peek=-1 read=-1 write=0",
        timeout=30,
    )
    dut.expect_exact("SPP_STREAM_DETACHED id=0", timeout=30)
