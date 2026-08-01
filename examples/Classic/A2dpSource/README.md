# A2dpSource

Enter a canonical Classic address such as `aa:bb:cc:dd:ee:ff` over Serial.
This example connects to that A2DP Sink and supplies silent PCM after calling
`startStream()`.

The PCM callback runs synchronously on the A2DP stack task. A real application
should copy from a bounded queue promptly, set `request.written`, and clear its
queue or resampler state when `request.flush` is true.

PCM is 16-bit interleaved; inspect `request.format` for the negotiated sample
rate and channel count. See the
[Japanese profile matrix](../../../docs/CLASSIC_PROFILE_SUPPORT.ja.md#a2dpのcore制約)
for Arduino-ESP32 3.3.11 limitations.
