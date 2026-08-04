import re


def test_generic_attribute_is_published_by_the_stack(dut, peers):
    """Service Changed belongs to Bluedroid here, not to the application.

    Arduino-ESP32 3.3.11 builds Bluedroid with
    CONFIG_BT_GATTS_SEND_SERVICE_CHANGE_AUTO=y, so the stack publishes Generic
    Attribute 0x1801 with Service Changed 0x2a05 and sends the indication itself
    when the local database changes. That is why this library has no
    notifyServicesChanged() counterpart to EspBle's: an application-registered
    0x1801 would be a second same-UUID service that UUID-based discovery never
    reaches, and registration is only allowed before begin(), so the application
    has no runtime change to announce anyway.

    The test pins both halves of that contract: the application registers no
    Generic Attribute service, and a peer still finds an indicatable Service
    Changed. If a future Core build turns the AUTO option off, this test is where
    it shows up.
    """
    peer = peers["device"]
    dut.expect_exact(
        "SERVICE_CHANGED_READY registered_generic_attribute=0", timeout=30
    )
    peer.expect_exact("SERVICE_CHANGED_PEER_READY", timeout=30)

    peer.write("c")
    marker = peer.expect(
        re.compile(rb"PEER_MARKER found=1 length=(\d+)"), timeout=40
    )
    assert int(marker.group(1)) == 2, (
        "the application's own service must be reachable, otherwise the "
        "Generic Attribute result below says nothing about ownership"
    )

    result = peer.expect(
        re.compile(
            rb"PEER_SERVICE_CHANGED service=(\d) characteristic=(\d) "
            rb"indicatable=(\d) handle=(\d+)"
        ),
        timeout=20,
    )
    assert result.group(1) == b"1", (
        "Generic Attribute 0x1801 is expected from the stack even though the "
        "application never registered it"
    )
    assert result.group(2) == b"1", "Service Changed 0x2a05 must be present"
    assert result.group(3) == b"1", "Service Changed must be indicatable"
    assert int(result.group(4)) != 0
    peer.expect_exact("PEER_DONE", timeout=10)
