import re


def test_public_gap_api_and_deferred_scan_callback(dut, peers):
    peripheral = peers["device"]

    dut.expect_exact("OVERSIZE_REJECTED", timeout=20)
    dut.expect_exact("CENTRAL_COMMAND_READY", timeout=20)
    peripheral.expect_exact("PERIPHERAL_ADVERTISING", timeout=20)
    dut.write("s")

    dut.expect_exact("SCAN_STARTED_NO_UPDATE", timeout=20)
    dut.expect_exact("PREBEGIN_REJECTED", timeout=20)
    # If onResult is called directly from the Bluedroid task, searching for
    # UPDATE_START consumes the earlier SCAN_RESULT and the next expect fails.
    dut.expect_exact("UPDATE_START", timeout=20)
    match = dut.expect(
        re.compile(
            rb"SCAN_RESULT name=Bluedroid Peer mfg=3412 rssi=-?\d+ "
            rb"connectable=1 scannable=1"
        ),
        timeout=30,
    )
    assert match is not None


def test_scan_duration_explicit_stop_and_end_flush(dut, peers):
    dut.write("t")
    dut.expect_exact("DURATION_SCAN started=1 stopped=1", timeout=20)
    dut.expect_exact("EXPLICIT_STOP started=1 stopped=1", timeout=20)
    dut.expect_exact(
        "END_SCAN started=1 ended=1 reinitialized=1 stale=0", timeout=20
    )
