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
    dut.expect_exact(
        "SPP_SECURITY_BONDS_CLEARED success=1 count=0",
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
    host_peer_address = host_compare.group(1)
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
    dut.expect(
        re.compile(rb"SPP_SECURITY_DISCONNECTED id=\d+"), timeout=30
    )

    dut.write("b")
    bond = dut.expect(
        re.compile(
            rb"SPP_SECURITY_BONDS count=1 listed=1 "
            rb"address=([0-9a-f:]{17})"
        ),
        timeout=30,
    )
    assert bond.group(1) == host_peer_address

    client.write(f"c{address}\n")
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
    dut.expect(
        re.compile(rb"SPP_SECURITY_DISCONNECTED id=\d+"), timeout=30
    )

    dut.write("d")
    dut.expect_exact(
        "SPP_SECURITY_BOND_DELETED success=1 count=0", timeout=30
    )
    client.write("b")
    client.expect(
        re.compile(
            rb"SPP_SECURITY_RAW_BONDS_CLEARED removed=\d+ count=0"
        ),
        timeout=30,
    )
    _exercise_secure_spp_client(dut, client)


def _exercise_secure_spp_client(dut, server):
    server.write("s")
    ready = server.expect(
        re.compile(
            rb"SPP_SECURITY_RAW_SERVER_READY "
            rb"address=([0-9a-f:]{17})"
        ),
        timeout=30,
    )
    address = ready.group(1).decode()

    dut.write("x")
    dut.expect_exact(
        "SPP_SECURITY_SERVER_STOPPED success=1", timeout=30
    )

    dut.write(f"c{address}\n")
    dut.expect_exact(
        "SPP_SECURITY_CLIENT_CONNECT accepted=1", timeout=30
    )
    client_compare = dut.expect(
        re.compile(
            rb"SPP_SECURITY_COMPARE address=[0-9a-f:]{17} "
            rb"value=(\d{6}) context=loop"
        ),
        timeout=30,
    )
    server_compare = server.expect(
        re.compile(rb"SPP_SECURITY_RAW_COMPARE value=(\d{6})"),
        timeout=30,
    )
    assert client_compare.group(1) == server_compare.group(1)
    dut.write("r")
    server.write("a")
    dut.expect_exact(
        "SPP_SECURITY_CONFIRM accepted=0 reply=1", timeout=30
    )
    server.expect_exact(
        "SPP_SECURITY_RAW_CONFIRM accepted=1 reply=1", timeout=30
    )
    dut.expect(
        re.compile(
            rb"SPP_SECURITY_CHANGED address=[0-9a-f:]{17} "
            rb"success=0 status=\d+ context=loop"
        ),
        timeout=30,
    )
    server.expect(
        re.compile(rb"SPP_SECURITY_RAW_AUTH success=0 status=\d+"),
        timeout=30,
    )
    dut.expect(
        re.compile(
            rb"SPP_SECURITY_CONNECTION_FAILED address="
            + address.encode()
            + rb" error=\d+ context=loop"
        ),
        timeout=30,
    )

    time.sleep(1)
    dut.write(f"c{address}\n")
    dut.expect_exact(
        "SPP_SECURITY_CLIENT_CONNECT accepted=1", timeout=30
    )
    client_compare = dut.expect(
        re.compile(
            rb"SPP_SECURITY_COMPARE address=[0-9a-f:]{17} "
            rb"value=(\d{6}) context=loop"
        ),
        timeout=30,
    )
    server_compare = server.expect(
        re.compile(rb"SPP_SECURITY_RAW_COMPARE value=(\d{6})"),
        timeout=30,
    )
    assert client_compare.group(1) == server_compare.group(1)
    dut.write("a")
    server.write("a")
    dut.expect_exact(
        "SPP_SECURITY_CONFIRM accepted=1 reply=1", timeout=30
    )
    server.expect_exact(
        "SPP_SECURITY_RAW_CONFIRM accepted=1 reply=1", timeout=30
    )
    dut.expect(
        re.compile(
            rb"SPP_SECURITY_CHANGED address=[0-9a-f:]{17} "
            rb"success=1 status=0 context=loop"
        ),
        timeout=30,
    )
    server.expect_exact(
        "SPP_SECURITY_RAW_AUTH success=1 status=0", timeout=30
    )
    connected = dut.expect(
        re.compile(
            rb"SPP_SECURITY_CONNECTED id=\d+ "
            rb"authenticated=1 encrypted=1 incoming=0"
        ),
        timeout=30,
    )
    assert connected
    server.expect_exact(
        "SPP_SECURITY_RAW_SERVER_CONNECTED", timeout=30
    )
    server.expect_exact(
        "SPP_SECURITY_RAW_RX length=3 hex=0043ff", timeout=30
    )
    server.expect_exact("SPP_SECURITY_RAW_DISCONNECTED", timeout=30)
    dut.expect(
        re.compile(rb"SPP_SECURITY_DISCONNECTED id=\d+"), timeout=30
    )
