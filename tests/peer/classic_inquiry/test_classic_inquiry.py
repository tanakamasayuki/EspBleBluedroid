import re


def test_classic_capabilities_and_inquiry(dut, peers):
    peripheral = peers["device"]
    peripheral.expect_exact("CLASSIC_PEER_READY", timeout=20)
    dut.write("i")
    dut.expect_exact(
        "CLASSIC_CAPABILITIES ble=1 classic=1 dual=1 inquiry=1 spp=0",
        timeout=20,
    )
    dut.expect_exact(
        "CLASSIC_PREBEGIN_REJECTED 1 error=InvalidState", timeout=20
    )
    dut.expect_exact("CLASSIC_CENTRAL_READY", timeout=20)
    dut.write("s")
    dut.expect_exact("CLASSIC_START_ACCEPTED 1", timeout=20)
    result = dut.expect(
        re.compile(
            rb"CLASSIC_RESULT address=([0-9a-f:]{17}) "
            rb"name=Bluedroid Classic Peer cod=(\d+) rssi=(-?\d+) "
            rb"has_cod=1 has_rssi=1 context=loop"
        ),
        timeout=20,
    )
    assert int(result.group(2)) != 0
    assert -128 <= int(result.group(3)) <= 127
    dut.expect_exact("CLASSIC_STOP_ACCEPTED 1", timeout=20)
    dut.expect_exact(
        "CLASSIC_COMPLETE cancelled=1 running=0 context=loop", timeout=20
    )
