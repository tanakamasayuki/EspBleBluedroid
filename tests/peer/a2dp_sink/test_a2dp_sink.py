import re


def test_public_a2dp_sink_receives_pcm_and_queues_control_events(dut, peers):
    source = peers["device"]
    source.write("i")
    source.expect(
        re.compile(rb"A2DP_RAW_READY state=0 address=[0-9a-f:]{17}"),
        timeout=30,
    )

    dut.write("i")
    dut.expect_exact(
        "A2DP_SINK_PREBEGIN_REJECTED 1 error=InvalidState", timeout=30
    )
    dut.expect_exact("A2DP_SINK_START_ACCEPTED 1", timeout=30)
    started = dut.expect(
        re.compile(
            rb"A2DP_SINK_STARTED success=1 address=([0-9a-f:]{17}) "
            rb"context=loop"
        ),
        timeout=30,
    )
    address = started.group(1).decode().replace(":", "")
    source.write(f"c{address}\n")

    source.expect(re.compile(rb"A2DP_RAW_CONNECTED mtu=\d+"), timeout=30)
    connected = dut.expect(
        re.compile(
            rb"A2DP_SINK_CONNECTED id=(\d+) address=[0-9a-f:]{17} "
            rb"incoming=1 mtu=(\d+) context=loop"
        ),
        timeout=30,
    )
    assert int(connected.group(1)) != 0
    assert int(connected.group(2)) >= 8
    source.expect(re.compile(rb"A2DP_RAW_PCM_REQUEST length=\d+"), timeout=30)
    dut.expect(
        re.compile(
            rb"A2DP_SINK_PCM id=\d+ length=\d+ rate=44100 channels=2 "
            rb"bits=16 zero=1 context=stack"
        ),
        timeout=30,
    )

    dut.write("d")
    dut.expect(re.compile(rb"A2DP_SINK_DISCONNECTED id=\d+ context=loop"), timeout=30)
    source.expect_exact("A2DP_RAW_DISCONNECTED", timeout=30)
    dut.write("e")
    dut.expect_exact("A2DP_SINK_END initialized=0", timeout=30)
