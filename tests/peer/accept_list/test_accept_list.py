import re


def test_accept_list_blocks_and_admits_connections(dut, peers):
    peripheral = peers["device"]

    dut.expect_exact("CENTRAL_READY", timeout=20)
    peripheral.expect_exact("PREBEGIN_REJECTED 1", timeout=20)
    peripheral.expect_exact(
        "ENTRY valid=1 address=02:00:00:00:00:01 type=0", timeout=20
    )
    peripheral.expect_exact(
        "MUTATION removed=1 missing=1 error=NotFound "
        "restored=1 cleared=0 readded=1",
        timeout=20,
    )
    peripheral.expect_exact("POLICY restricted entries=1", timeout=20)
    peripheral.write("?")
    peripheral.expect_exact("ADVERTISING 1", timeout=10)

    dut.write("c")
    dut.expect_exact("SCAN_STARTED", timeout=10)
    # Bluedroid may report the controller rejection immediately as a backend
    # failure, or let the public request deadline classify it as a timeout.
    dut.expect(
        re.compile(rb"CENTRAL_CONNECT_FAILED error=(3|6)"), timeout=15
    )

    peripheral.write("o")
    peripheral.expect_exact("POLICY open entries=1", timeout=10)
    dut.write("c")
    dut.expect_exact("SCAN_STARTED", timeout=10)
    dut.expect("CENTRAL_CONNECTED id=", timeout=20)

    dut.write("d")
    dut.expect_exact("DISCONNECT_REQUESTED", timeout=10)
    dut.expect("CENTRAL_DISCONNECTED id=", timeout=15)
