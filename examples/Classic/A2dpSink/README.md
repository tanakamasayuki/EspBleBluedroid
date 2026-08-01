# A2dpSink

This minimal example receives music from an A2DP Source and exposes the
16-bit interleaved PCM decoded from SBC by the Core through `onPcmData()`.

The PCM callback runs synchronously on the A2DP stack task, and its pointer is
valid only during the callback. A real application should copy it into a
bounded audio queue and perform I2S or USB Audio work on another task.

Connection and stream-control callbacks are delivered by `bluetooth.update()`.
See the [Japanese profile matrix](../../../docs/CLASSIC_PROFILE_SUPPORT.ja.md#a2dpのcore制約)
for Arduino-ESP32 3.3.11 limitations.
