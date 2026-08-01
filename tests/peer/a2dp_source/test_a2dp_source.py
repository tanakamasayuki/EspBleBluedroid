import re


def test_public_a2dp_source_supplies_pcm_and_queues_control_events(dut, peers):
    sink = peers["device"]
    sink.write("i")
    ready = sink.expect(
        re.compile(rb"A2DP_RAW_SINK_READY address=([0-9a-f:]{17})"),
        timeout=30,
    )
    address = ready.group(1).decode()

    dut.write("i")
    dut.expect_exact(
        "A2DP_SOURCE_PREBEGIN_REJECTED 1 error=InvalidState", timeout=30
    )
    dut.expect_exact("AVRCP_TG_START_ACCEPTED 1", timeout=30)
    dut.expect_exact("A2DP_SOURCE_START_ACCEPTED 1", timeout=30)
    dut.expect(
        re.compile(
            rb"A2DP_SOURCE_STARTED success=1 address=[0-9a-f:]{17} "
            rb"context=loop"
        ),
        timeout=30,
    )
    dut.write(f"c{address}\n")
    dut.expect_exact("A2DP_SOURCE_CONNECT_ACCEPTED 1", timeout=30)

    sink.expect(re.compile(rb"A2DP_RAW_SINK_CONNECTED mtu=\d+"), timeout=30)
    connected = dut.expect(
        re.compile(
            rb"A2DP_SOURCE_CONNECTED id=(\d+) address=[0-9a-f:]{17} "
            rb"incoming=0 mtu=(\d+) context=loop"
        ),
        timeout=30,
    )
    assert int(connected.group(1)) != 0
    assert int(connected.group(2)) >= 8
    dut.expect_exact("A2DP_SOURCE_STREAM_REQUEST 1", timeout=30)
    dut.expect(
        re.compile(
            rb"A2DP_SOURCE_PCM id=\d+ capacity=\d+ rate=44100 channels=2 "
            rb"bits=16 context=stack"
        ),
        timeout=30,
    )
    sink.expect(
        re.compile(rb"A2DP_RAW_SINK_PCM length=\d+ zero=1"), timeout=30
    )
    dut.expect(
        re.compile(rb"AVRCP_TG_CONNECTED address=[0-9a-f:]{17} context=loop"),
        timeout=30,
    )
    sink.expect_exact("AVRCP_RAW_CT_CONNECTED", timeout=30)
    sink.write("p")
    dut.expect_exact("AVRCP_TG_COMMAND command=70 state=0 context=loop", timeout=30)
    dut.expect_exact("AVRCP_TG_COMMAND command=70 state=1 context=loop", timeout=30)
    sink.write("v")
    dut.expect_exact("AVRCP_TG_VOLUME volume=91 context=loop", timeout=30)

    dut.write("d")
    dut.expect(
        re.compile(rb"A2DP_SOURCE_DISCONNECTED id=\d+ context=loop"),
        timeout=30,
    )
    sink.expect_exact("A2DP_RAW_SINK_DISCONNECTED", timeout=30)
    dut.write("e")
    dut.expect_exact("A2DP_SOURCE_END initialized=0", timeout=30)
