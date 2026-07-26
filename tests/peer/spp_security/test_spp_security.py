import re
import time


def test_spp_server_numeric_comparison_rejection_then_success(dut, peers):
    client = peers["device"]
    client.write("i")
    client.expect(
        re.compile(rb"SPP_SECURITY_RAW_READY bonds_cleared=\d+"),
        timeout=30,
    )

    dut.write("i")
    dut.expect(
        re.compile(rb"SPP_SECURITY_BONDS_CLEARED \d+"),
        timeout=30,
    )
    ready = dut.expect(
        re.compile(rb"SPP_SECURITY_READY address=([0-9a-f:]{17})"),
        timeout=30,
    )
    address = ready.group(1).decode().replace(":", "")

    client.write(f"c{address}\n")
    host_compare = dut.expect(
        re.compile(
            rb"SPP_SECURITY_COMPARE address=([0-9a-f:]{17}) "
            rb"value=(\d{6}) context=loop"
        ),
        timeout=30,
    )
    peer_compare = client.expect(
        re.compile(rb"SPP_SECURITY_RAW_COMPARE value=(\d{6})"),
        timeout=30,
    )
    assert host_compare.group(2) == peer_compare.group(1)
    dut.write("r")
    client.write("a")
    dut.expect_exact(
        "SPP_SECURITY_CONFIRM accepted=0 reply=1", timeout=30
    )
    client.expect_exact(
        "SPP_SECURITY_RAW_CONFIRM accepted=1 reply=1", timeout=30
    )
    dut.expect(
        re.compile(
            rb"SPP_SECURITY_CHANGED address=[0-9a-f:]{17} "
            rb"success=0 status=\d+ context=loop"
        ),
        timeout=30,
    )
    client.expect(
        re.compile(rb"SPP_SECURITY_RAW_AUTH success=0 status=\d+"),
        timeout=30,
    )
    client.expect_exact("SPP_SECURITY_RAW_DISCONNECTED", timeout=30)

    time.sleep(1)
    client.write(f"c{address}\n")
    host_compare = dut.expect(
        re.compile(
            rb"SPP_SECURITY_COMPARE address=[0-9a-f:]{17} "
            rb"value=(\d{6}) context=loop"
        ),
        timeout=30,
    )
    peer_compare = client.expect(
        re.compile(rb"SPP_SECURITY_RAW_COMPARE value=(\d{6})"),
        timeout=30,
    )
    assert host_compare.group(1) == peer_compare.group(1)
    dut.write("a")
    client.write("a")
    dut.expect_exact(
        "SPP_SECURITY_CONFIRM accepted=1 reply=1", timeout=30
    )
    client.expect_exact(
        "SPP_SECURITY_RAW_CONFIRM accepted=1 reply=1", timeout=30
    )
    dut.expect(
        re.compile(
            rb"SPP_SECURITY_CHANGED address=[0-9a-f:]{17} "
            rb"success=1 status=0 context=loop"
        ),
        timeout=30,
    )
    client.expect_exact(
        "SPP_SECURITY_RAW_AUTH success=1 status=0", timeout=30
    )
    dut.expect(
        re.compile(
            rb"SPP_SECURITY_CONNECTED id=\d+ "
            rb"authenticated=1 encrypted=1"
        ),
        timeout=30,
    )
    client.expect_exact("SPP_SECURITY_RAW_CONNECTED", timeout=30)
    client.expect_exact(
        "SPP_SECURITY_RAW_RX length=3 hex=0053ff", timeout=30
    )
    client.expect_exact("SPP_SECURITY_RAW_DISCONNECTED", timeout=30)
