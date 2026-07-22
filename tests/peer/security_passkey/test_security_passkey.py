def test_static_passkey_mitm(dut, peers):
    peripheral = peers["device"]
    dut.expect_exact("PASSKEY_CENTRAL_READY", timeout=20)
    peripheral.expect_exact("PASSKEY_PEER_READY", timeout=20)

    dut.write("x")
    peripheral.write("x")
    dut.expect_exact(
        "PASSKEY_CENTRAL_BONDS_CLEARED success=1 count=0", timeout=20
    )
    peripheral.expect_exact(
        "PASSKEY_PEER_BONDS_CLEARED success=1 count=0", timeout=20
    )

    dut.write("s")
    dut.expect_exact("PASSKEY_SCAN_STARTED 1", timeout=20)
    dut.expect_exact("PASSKEY_CONNECT_REQUESTED 1", timeout=30)
    peripheral.expect_exact("PASSKEY_PEER_CONNECTED", timeout=20)
    dut.expect_exact("PASSKEY_CONNECTED id=1 context=loop", timeout=20)
    dut.expect_exact(
        "PASSKEY_DISPLAYED id=1 passkey=438209 context=loop", timeout=30
    )
    dut.expect_exact(
        "PASSKEY_SECURITY success=1 encrypted=1 authenticated=1 bonded=1 key=16 context=loop",
        timeout=30,
    )
    peripheral.expect_exact(
        "PEER_PASSKEY_SECURITY success=1 encrypted=1 authenticated=1 bonded=1 key=16",
        timeout=30,
    )
    dut.expect_exact("PASSKEY_DISCOVERY_REQUESTED 1", timeout=20)
    dut.expect_exact("PASSKEY_DISCOVERY success=1 context=loop", timeout=30)
    dut.expect_exact("AUTH_READ_REQUESTED 1", timeout=20)
    dut.expect_exact(
        "AUTH_READ success=1 value=authenticated-ready context=loop", timeout=20
    )
    dut.expect_exact("AUTH_WRITE_REQUESTED 1", timeout=20)
    peripheral.expect_exact(
        "AUTHENTICATED_PEER_WRITE value=authenticated-write authenticated=1",
        timeout=20,
    )
    dut.expect_exact("AUTH_WRITE success=1 context=loop", timeout=20)

    dut.write("b")
    peripheral.write("b")
    dut.expect_exact("PASSKEY_CENTRAL_BONDS count=1", timeout=20)
    peripheral.expect_exact("PASSKEY_PEER_BONDS count=1", timeout=20)

    dut.write("d")
    dut.expect_exact("PASSKEY_DISCONNECT_REQUESTED 1", timeout=20)
    dut.expect_exact(
        "PASSKEY_DISCONNECTED id=1 authenticated=1 context=loop", timeout=20
    )
    peripheral.expect_exact(
        "PASSKEY_PEER_DISCONNECTED authenticated=1", timeout=20
    )

    dut.write("x")
    peripheral.write("x")
    dut.expect_exact(
        "PASSKEY_CENTRAL_BONDS_CLEARED success=1 count=0", timeout=20
    )
    peripheral.expect_exact(
        "PASSKEY_PEER_BONDS_CLEARED success=1 count=0", timeout=20
    )
