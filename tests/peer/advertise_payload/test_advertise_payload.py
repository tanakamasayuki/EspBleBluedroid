import re
import time

PAYLOAD_PATTERN = re.compile(rb"SCANNER_PAYLOAD len=(\d+) hex=([0-9a-f]+)")


def _parse_ad_structures(payload: bytes):
    structures = []
    offset = 0
    while offset < len(payload):
        length = payload[offset]
        if length == 0:
            break
        assert offset + 1 + length <= len(payload), "truncated AD structure"
        structures.append(
            (payload[offset + 1], payload[offset + 2 : offset + 1 + length])
        )
        offset += 1 + length
    return structures


def test_legacy_payload_boundaries_and_timed_stop(dut, peers):
    device = peers["device"]
    dut.expect_exact("SCANNER_READY", timeout=10)
    device.expect_exact("DEVICE_READY", timeout=10)

    # CSS Part A 1.1: both 16-bit UUIDs belong in one Complete List AD type.
    device.write("a")
    device.expect_exact("DEVICE_MULTI_UUID success=1 error=None", timeout=10)
    dut.write("s")
    dut.expect_exact("SCANNER_STARTED success=1", timeout=10)
    match = dut.expect(PAYLOAD_PATTERN, timeout=30)
    payload = bytes.fromhex(match.group(2).decode())
    structures = _parse_ad_structures(payload)
    complete16 = [data for ad_type, data in structures if ad_type == 0x03]
    assert len(complete16) == 1
    uuids = {
        int.from_bytes(complete16[0][index : index + 2], "little")
        for index in range(0, len(complete16[0]), 2)
    }
    assert uuids == {0x1812, 0x180F}
    types = [ad_type for ad_type, _ in structures]
    assert len(types) == len(set(types)), f"duplicate AD types: {types}"

    device.write("q")
    device.expect_exact("DEVICE_STOPPED success=1 error=None", timeout=10)
    device.write("b")
    device.expect_exact("DEVICE_BOUNDARY success=1 error=None", timeout=10)
    device.write("x")
    device.expect_exact(
        "DEVICE_OVERFLOW success=0 error=InvalidArgument", timeout=10
    )

    device.write("t")
    device.expect_exact("DEVICE_TIMED success=1 error=None", timeout=10)
    device.write("?")
    device.expect_exact("DEVICE_ADVERTISING 1", timeout=10)
    # The device calls update() continuously; a one-second duration must expire.
    time.sleep(1.2)
    device.write("?")
    device.expect_exact("DEVICE_ADVERTISING 0", timeout=10)
