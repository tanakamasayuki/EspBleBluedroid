import re

# The UUID both parsers must report for a Service Data block advertised as the
# 16-bit 0x180F, expanded to the Bluetooth base UUID.
SERVICE_DATA_UUID_128 = "0000180f-0000-1000-8000-00805f9b34fb"


def test_advertising_payloads_survive_between_the_two_stacks(dut, peers):
    """An advertisement built by one stack, reconstructed by the other's parser.

    Both directions run, because a payload builder and a payload parser can agree
    with each other while both disagree with the rest of the world: the fields are
    asserted by value, including the exact Service Data bytes and the 128-bit form
    of the 16-bit UUID they were advertised under.

    The passive scan is the part that cannot be checked with two boards of the
    same stack: it must see the advertising payload's fields and nothing that was
    put in the scan response, which is what proves the active-scan merge combines
    two payloads received on the air rather than reporting one of them twice.

    `dut` is the ESP32-S3 running EspBle and `peers["device"]` is the original
    ESP32 running the library under test.
    """
    espble = dut
    bluedroid = peers["device"]

    # The EspBle board is asked to report rather than being waited on: it boots
    # while the other board is still being flashed, so the startup line alone
    # would depend on when the monitor started reading.
    espble.write("?")
    espble.expect_exact("ESPBLE_READY_STATE ready=1", timeout=40)
    bluedroid.expect_exact("INTEROP_ADVERTISE_SCAN_READY", timeout=40)

    # Direction 1: EspBle advertises, this library scans.
    espble.write("a")
    espble.expect_exact("ESPBLE_ADVERTISING 1", timeout=10)

    bluedroid.write("s")
    bluedroid.expect_exact("SCAN_STARTED 1", timeout=10)
    bluedroid.expect_exact(
        "SCAN_RESULT name=[EspBle Adv] mfg=e5021101 "
        "sd_uuid=%s sd=64010a appearance=03c1 tx_present=1 "
        "connectable=1 scannable=1" % SERVICE_DATA_UUID_128,
        timeout=40,
    )

    # The same advertiser seen passively: the advertising payload only. A parser
    # that reported the scan response's fields here would be reporting a merge it
    # never received.
    bluedroid.write("q")
    bluedroid.expect_exact("PASSIVE_SCAN_STARTED 1", timeout=10)
    bluedroid.expect_exact(
        "PASSIVE_RESULT name=[] mfg= sd_uuid=none sd= "
        "appearance=03c1 tx_present=1 connectable=1 scannable=1",
        timeout=40,
    )

    espble.write("A")
    espble.expect_exact("ESPBLE_ADVERTISING_STOPPED 1", timeout=10)

    # Direction 2: this library advertises, EspBle scans. The builder is the one
    # under test now, and the field values are the other set so a direction mixed
    # up somewhere cannot pass.
    bluedroid.write("p")
    bluedroid.expect_exact("ADVERTISING 1", timeout=10)

    espble.write("s")
    espble.expect_exact("ESPBLE_SCAN_STARTED 1", timeout=10)
    espble.expect_exact(
        "ESPBLE_SCAN_RESULT name=[Bluedroid Adv] mfg=e5022202 "
        "sd_uuid=%s sd=32020b appearance=0442 tx_present=1 "
        "connectable=1 scannable=1" % SERVICE_DATA_UUID_128,
        timeout=40,
    )

    bluedroid.write("P")
    bluedroid.expect(re.compile(rb"ADVERTISING_STOPPED 1"), timeout=10)
