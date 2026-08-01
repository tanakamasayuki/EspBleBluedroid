# A2dpSink

A2DP Sourceから音楽を受信し、CoreがSBCから復号した16-bit interleaved PCMを
`onPcmData()`で受け取る最小例です。

PCM callbackはA2DP stack task上で同期実行されます。pointerはcallback中だけ有効です。
実際のapplicationではcallback内でSerial出力や再生処理を行わず、固定容量audio queueへ
copyし、別taskでI2SやUSB Audioへ渡してください。

接続、切断、stream状態などのcontrol callbackは`bluetooth.update()`から呼ばれます。
Arduino-ESP32 3.3.11標準buildの制約は
[Classic profile対応表](../../../docs/CLASSIC_PROFILE_SUPPORT.ja.md#a2dpのcore制約)を参照してください。
