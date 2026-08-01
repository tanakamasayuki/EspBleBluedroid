import re


def test_public_hfp_hf_ag_slc_and_bidirectional_pcm(dut, peers):
    gateway = peers["device"]
    gateway.write("i")
    ready = gateway.expect(
        re.compile(rb"HFP_AG_READY address=([0-9a-f:]{17})"), timeout=30
    )
    address = ready.group(1).decode()

    dut.write("i")
    dut.expect(re.compile(rb"HFP_HF_READY address=[0-9a-f:]{17}"), timeout=30)
    dut.write(f"c{address}\n")
    dut.expect_exact("HFP_HF_CONNECT 1", timeout=30)
    dut.expect_exact("HFP_HF_CONNECTION state=3", timeout=30)
    gateway.expect_exact("HFP_AG_CONNECTION state=3", timeout=30)

    dut.write("a")
    dut.expect_exact("HFP_HF_AUDIO_CONNECT 1", timeout=30)
    dut.expect(
        re.compile(rb"HFP_HF_AUDIO state=(2|3) frame=(\d+) handle=\d+"),
        timeout=30,
    )
    gateway.expect(
        re.compile(rb"HFP_AG_AUDIO state=(2|3) frame=(\d+) handle=\d+"),
        timeout=30,
    )
    # Arduino-ESP32's built-in CVSD/mSBC codec uses the legacy PCM callbacks.
    # In that mode the Core reports a preferred frame size of zero, so the
    # callback's actual byte count is the only portable framing contract.
    dut.expect(re.compile(rb"HFP_HF_PCM bytes=\d+ rate=(8000|16000)"), timeout=30)
    gateway.expect(re.compile(rb"HFP_AG_PCM bytes=\d+ rate=(8000|16000)"), timeout=30)

    dut.write("d")
    dut.expect_exact("HFP_HF_CONNECTION state=0", timeout=30)
    gateway.expect_exact("HFP_AG_CONNECTION state=0", timeout=30)
