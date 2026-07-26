import re


def test_classic_display_and_keyboard_passkey_pairing(dut, peers):
    client = peers["device"]
    client.write("i")
    client.expect_exact("SPP_PASSKEY_RAW_READY", timeout=30)

    dut.write("i")
    dut.expect_exact(
        "SPP_PASSKEY_INVALID_REJECTED 1 error=InvalidArgument",
        timeout=30,
    )
    ready = dut.expect(
        re.compile(rb"SPP_PASSKEY_READY address=([0-9a-f:]{17})"),
        timeout=30,
    )
    address = ready.group(1).decode().replace(":", "")

    client.write(f"c{address}\n")
    displayed = client.expect(
        re.compile(rb"SPP_PASSKEY_RAW_DISPLAYED passkey=(\d{6})"),
        timeout=30,
    )
    requested = dut.expect(
        re.compile(
            rb"SPP_PASSKEY_REQUESTED address=([0-9a-f:]{17}) "
            rb"context=loop"
        ),
        timeout=30,
    )
    assert requested.group(1)
    passkey = displayed.group(1).decode()
    dut.write(f"k{passkey}\n")
    dut.expect_exact(
        f"SPP_PASSKEY_PROVIDED accepted=1 passkey={passkey}",
        timeout=30,
    )
    dut.expect(
        re.compile(
            rb"SPP_PASSKEY_SECURITY address=[0-9a-f:]{17} "
            rb"success=1 status=0 context=loop"
        ),
        timeout=30,
    )
    client.expect_exact(
        "SPP_PASSKEY_RAW_SECURITY success=1 status=0", timeout=30
    )
    dut.expect(
        re.compile(
            rb"SPP_PASSKEY_CONNECTED id=\d+ authenticated=1 "
            rb"encrypted=1 incoming=1 context=loop"
        ),
        timeout=30,
    )
    client.expect_exact("SPP_PASSKEY_RAW_CONNECTED", timeout=30)
    client.expect_exact(
        "SPP_PASSKEY_RAW_RX length=3 hex=0050ff", timeout=30
    )
    client.expect_exact("SPP_PASSKEY_RAW_DISCONNECTED", timeout=30)
    dut.expect(
        re.compile(rb"SPP_PASSKEY_DISCONNECTED id=\d+ context=loop"),
        timeout=30,
    )

    client.write("b")
    client.expect_exact("SPP_PASSKEY_RAW_BONDS count=0", timeout=30)
    client.write("m")
    client.expect_exact(
        "SPP_PASSKEY_RAW_KEYBOARD success=1", timeout=30
    )
    dut.write("r")
    dut.expect_exact(
        "SPP_PASSKEY_DISPLAY_RESTART cleared=1 restarted=1",
        timeout=30,
    )
    ready = dut.expect(
        re.compile(rb"SPP_PASSKEY_READY address=([0-9a-f:]{17})"),
        timeout=30,
    )
    address = ready.group(1).decode().replace(":", "")

    client.write(f"c{address}\n")
    displayed = dut.expect(
        re.compile(
            rb"SPP_PASSKEY_DISPLAYED address=[0-9a-f:]{17} "
            rb"passkey=(\d{6}) context=loop"
        ),
        timeout=30,
    )
    client.expect_exact("SPP_PASSKEY_RAW_REQUESTED", timeout=30)
    passkey = displayed.group(1).decode()
    client.write(f"k{passkey}\n")
    client.expect_exact(
        f"SPP_PASSKEY_RAW_PROVIDED accepted=1 passkey={passkey}",
        timeout=30,
    )
    dut.expect(
        re.compile(
            rb"SPP_PASSKEY_SECURITY address=[0-9a-f:]{17} "
            rb"success=1 status=0 context=loop"
        ),
        timeout=30,
    )
    client.expect_exact(
        "SPP_PASSKEY_RAW_SECURITY success=1 status=0", timeout=30
    )
    dut.expect(
        re.compile(
            rb"SPP_PASSKEY_CONNECTED id=\d+ authenticated=1 "
            rb"encrypted=1 incoming=1 context=loop"
        ),
        timeout=30,
    )
    client.expect_exact("SPP_PASSKEY_RAW_CONNECTED", timeout=30)
    client.expect_exact(
        "SPP_PASSKEY_RAW_RX length=3 hex=0050ff", timeout=30
    )
    client.expect_exact("SPP_PASSKEY_RAW_DISCONNECTED", timeout=30)
    dut.expect(
        re.compile(rb"SPP_PASSKEY_DISCONNECTED id=\d+ context=loop"),
        timeout=30,
    )
