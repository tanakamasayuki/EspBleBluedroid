def test_gatt_server_read_write_descriptor_and_notify(dut, peers):
    peer = peers["device"]
    dut.expect_exact("GATT_SERVER_READY", timeout=30)
    peer.expect_exact("GATT_SERVER_PEER_READY", timeout=30)
    peer.write("c")
    peer.expect_exact("PEER_READ length=3 hex=5200fe", timeout=40)
    dut.expect(rb"SERVER_WRITE id=\d+ length=3 hex=5700fc context=loop", timeout=20)
    dut.expect_exact("SERVER_DESCRIPTOR_WRITE length=3 context=loop", timeout=20)
    peer.expect_exact("PEER_SUBSCRIBED", timeout=20)
    dut.expect_exact(
        "SERVER_SUBSCRIPTION notifications=1 context=loop", timeout=20
    )
    dut.write("n")
    dut.expect_exact("SERVER_NOTIFY_ACCEPTED 1", timeout=20)
    peer.expect_exact(
        "PEER_NOTIFICATION length=3 hex=4e00fd indication=0", timeout=20
    )
    dut.expect_exact("SERVER_SENT success=1 indication=0 context=loop", timeout=20)
