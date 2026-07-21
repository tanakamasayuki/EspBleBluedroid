def test_bluedroid_connect_gatt_and_notify(dut, peers):
    peripheral = peers["device"]

    # Serial monitors can attach after setup-only readiness messages, so use
    # connection/data-path events as the synchronization points.
    dut.expect_exact("SCAN_FOUND", timeout=30)
    dut.expect_exact("CONNECTED", timeout=20)
    dut.expect_exact("READ peer-ready", timeout=20)
    dut.expect_exact("SUBSCRIBED", timeout=20)
    # Bluedroid can dispatch the notification callback before the synchronous
    # writeValue() call returns, so these two Central-side lines are unordered.
    dut.expect_exact(
        ["WRITE 1", "NOTIFY peer-notify"], expect_all=True, timeout=20
    )
    peripheral.expect_exact("WRITE central-write", timeout=20)
    peripheral.expect_exact("NOTIFY_SENT", timeout=20)
