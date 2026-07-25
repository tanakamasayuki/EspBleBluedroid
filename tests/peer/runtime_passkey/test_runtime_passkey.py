import re


def test_runtime_passkey_entry(dut, peers):
    peripheral = peers["device"]
    dut.expect_exact(
        "RUNTIME_PASSKEY_INVALID_REJECTED 1 error=InvalidArgument", timeout=20
    )
    dut.expect_exact("RUNTIME_PASSKEY_CENTRAL_READY", timeout=20)
    peripheral.expect_exact("RUNTIME_PASSKEY_PEER_READY", timeout=20)
    dut.write("x")
    peripheral.write("x")
    dut.expect_exact("RUNTIME_CENTRAL_BONDS_CLEARED success=1 count=0", timeout=20)
    peripheral.expect_exact("RUNTIME_PEER_BONDS_CLEARED success=1 count=0", timeout=20)

    dut.write("s")
    dut.expect_exact("RUNTIME_PASSKEY_SCAN_STARTED 1", timeout=20)
    dut.expect_exact("RUNTIME_PASSKEY_CONNECT_REQUESTED 1", timeout=30)
    peripheral.expect_exact("RUNTIME_PASSKEY_PEER_CONNECTED", timeout=20)
    dut.expect_exact("RUNTIME_PASSKEY_CONNECTED id=1", timeout=20)
    displayed = peripheral.expect(
        re.compile(rb"RUNTIME_PASSKEY_DISPLAYED passkey=(\d{6})"), timeout=30
    )
    passkey = displayed.group(1).decode()
    dut.write("k" + passkey + "\n")
    dut.expect_exact(
        "RUNTIME_PASSKEY_PROVIDED accepted=1 passkey=" + passkey, timeout=20
    )
    dut.expect_exact(
        "RUNTIME_PASSKEY_SECURITY success=1 encrypted=1 authenticated=1 bonded=1 key=16",
        timeout=30,
    )
    peripheral.expect_exact(
        "RUNTIME_PEER_SECURITY success=1 encrypted=1 authenticated=1 bonded=1 key=16",
        timeout=30,
    )

    dut.write("d")
    dut.expect_exact("RUNTIME_PASSKEY_DISCONNECT_REQUESTED 1", timeout=20)
    dut.expect_exact(
        "RUNTIME_PASSKEY_DISCONNECTED id=1 authenticated=1", timeout=20
    )
    peripheral.expect_exact(
        "RUNTIME_PASSKEY_PEER_DISCONNECTED authenticated=1", timeout=20
    )
    dut.write("x")
    peripheral.write("x")
    dut.expect_exact("RUNTIME_CENTRAL_BONDS_CLEARED success=1 count=0", timeout=20)
    peripheral.expect_exact("RUNTIME_PEER_BONDS_CLEARED success=1 count=0", timeout=20)
