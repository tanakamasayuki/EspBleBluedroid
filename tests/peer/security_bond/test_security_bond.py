def expect_secure_connection(dut, peripheral, connection_id, initial_value):
    dut.expect_exact(
        f"CENTRAL_CONNECTED id={connection_id} encrypted=0 bonded=0 context=loop",
        timeout=30,
    )
    dut.expect_exact(
        "CENTRAL_SECURITY success=1 encrypted=1 authenticated=0 bonded=1 key=16 stored=1 context=loop",
        timeout=30,
    )
    peripheral.expect_exact(
        "PERIPHERAL_SECURITY success=1 encrypted=1 authenticated=0 bonded=1 key=16",
        timeout=30,
    )
    dut.expect_exact("DISCOVERY_REQUESTED 1", timeout=20)
    dut.expect_exact("DISCOVERY success=1 context=loop", timeout=30)
    dut.expect_exact("SECURE_READ_REQUESTED 1", timeout=20)
    dut.expect_exact(
        f"SECURE_READ success=1 value={initial_value} context=loop", timeout=20
    )
    dut.expect_exact("SECURE_WRITE_REQUESTED 1", timeout=20)
    peripheral.expect_exact(
        "SECURE_PEER_WRITE value=central-secure-write secured=1", timeout=20
    )
    dut.expect_exact("SECURE_WRITE success=1 context=loop", timeout=20)


def test_just_works_bond_and_reconnect(dut, peers):
    peripheral = peers["device"]
    dut.expect_exact(
        "DISABLED_SECURITY_OPTIONS_REJECTED 1 error=InvalidArgument", timeout=20
    )
    dut.expect_exact("MITM_REJECTED 1 error=Unsupported", timeout=20)
    dut.expect_exact("SECURITY_CENTRAL_READY", timeout=20)
    peripheral.expect_exact("SECURITY_PERIPHERAL_READY", timeout=20)

    dut.write("x")
    peripheral.write("x")
    dut.expect_exact("CENTRAL_BONDS_CLEARED success=1 count=0", timeout=20)
    peripheral.expect_exact("PERIPHERAL_BONDS_CLEARED success=1 count=0", timeout=20)

    dut.write("s")
    dut.expect_exact("SCAN_STARTED 1", timeout=20)
    dut.expect_exact("CONNECT_REQUESTED 1", timeout=30)
    peripheral.expect_exact("PERIPHERAL_CONNECTED", timeout=20)
    expect_secure_connection(dut, peripheral, 1, "secure-ready")

    dut.write("b")
    peripheral.write("b")
    dut.expect_exact("CENTRAL_BONDS count=1 listed=1", timeout=20)
    peripheral.expect_exact("PERIPHERAL_BONDS count=1", timeout=20)

    dut.write("d")
    dut.expect_exact("DISCONNECT_REQUESTED 1", timeout=20)
    dut.expect_exact(
        "CENTRAL_DISCONNECTED id=1 encrypted=1 bonded=1 context=loop", timeout=20
    )
    peripheral.expect_exact("PERIPHERAL_DISCONNECTED secured=1", timeout=20)

    peripheral.write("a")
    peripheral.expect_exact("PERIPHERAL_ADVERTISING", timeout=20)
    dut.write("s")
    dut.expect_exact("SCAN_STARTED 1", timeout=20)
    dut.expect_exact("CONNECT_REQUESTED 1", timeout=30)
    peripheral.expect_exact("PERIPHERAL_CONNECTED", timeout=20)
    expect_secure_connection(dut, peripheral, 2, "central-secure-write")

    dut.write("d")
    dut.expect_exact("DISCONNECT_REQUESTED 1", timeout=20)
    dut.expect_exact(
        "CENTRAL_DISCONNECTED id=2 encrypted=1 bonded=1 context=loop", timeout=20
    )
    peripheral.expect_exact("PERIPHERAL_DISCONNECTED secured=1", timeout=20)

    dut.write("y")
    peripheral.write("x")
    dut.expect_exact("CENTRAL_BOND_DELETED success=1 count=0", timeout=20)
    peripheral.expect_exact("PERIPHERAL_BONDS_CLEARED success=1 count=0", timeout=20)
