import re
import time


def test_high_duty_directed_advertising_connects_only_target(dut, peers):
    peripheral = peers["device"]

    central_ready = dut.expect(
        re.compile(rb"CENTRAL_READY address=([0-9a-f:]{17})"), timeout=20
    )
    central_address = central_ready.group(1).decode()
    peripheral.expect_exact(
        "VALIDATION prebegin=1 payload_ignored=1 invalid_address=1 "
        "invalid_channel=1 channel39=1",
        timeout=20,
    )
    peripheral_ready = peripheral.expect(
        re.compile(rb"PERIPHERAL_READY address=([0-9a-f:]{17})"), timeout=20
    )
    peripheral_address = peripheral_ready.group(1).decode()
    dut.write(f"p{peripheral_address}\n")
    dut.expect_exact(f"PEER_SET address={peripheral_address}", timeout=10)

    dut.write("s")
    dut.expect_exact("SCAN_STARTED", timeout=10)
    peripheral.write(f"h{central_address}\n")
    peripheral.expect_exact(
        f"DIRECTED_STARTED success=1 mode=high target={central_address} "
        "advertising=1 error=NONE",
        timeout=10,
    )
    dut.expect(
        re.compile(
            rb"DIRECTED_RESULT address=[0-9a-f:]{17} type=[01] "
            rb"connectable=1 scannable=0 name=0 manufacturer=0 "
            rb"services=0 service_data=0"
        ),
        timeout=15,
    )
    dut.expect(re.compile(rb"CENTRAL_CONNECTED id=(\d+) mtu=23"), timeout=20)

    dut.write("d")
    dut.expect_exact("DISCONNECT_REQUESTED", timeout=10)
    dut.expect(re.compile(rb"CENTRAL_DISCONNECTED id=\d+"), timeout=15)

    time.sleep(1.5)
    peripheral.write(f"h{central_address}\n")
    peripheral.expect_exact(
        f"DIRECTED_STARTED success=1 mode=high target={central_address} "
        "advertising=1 error=NONE",
        timeout=10,
    )
    time.sleep(1.5)
    peripheral.write("?\n")
    peripheral.expect_exact("ADVERTISING 0", timeout=10)

    peripheral.write(f"l{central_address}\n")
    peripheral.expect_exact(
        f"DIRECTED_STARTED success=1 mode=low target={central_address} "
        "advertising=1 error=NONE",
        timeout=10,
    )
    time.sleep(1.5)
    peripheral.write("?\n")
    peripheral.expect_exact("ADVERTISING 1", timeout=10)
    peripheral.write("x\n")
    peripheral.expect_exact(
        "DIRECTED_STOPPED success=1 advertising=0", timeout=10
    )
