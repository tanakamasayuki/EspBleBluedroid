# A2dpSource

Serialへ入力した`aa:bb:cc:dd:ee:ff`形式のClassic addressへ接続し、無音PCMをA2DPで
送信する最小例です。接続後に`startStream()`を呼び、Coreからの`onPcmRequested()`を
満たします。

PCM callbackはA2DP stack task上で同期実行されます。実際のapplicationでは固定容量queueから
すぐにPCMをcopyし、`request.written`へ書き込んだbyte数を設定してください。`flush`時は
application側のqueueやresampler状態を破棄します。

この例のPCMは16-bit interleavedです。sample rateとchannel数は`request.format`で確認できます。
Arduino-ESP32 3.3.11標準buildの制約は
[Classic profile対応表](../../../docs/CLASSIC_PROFILE_SUPPORT.ja.md#a2dpのcore制約)を参照してください。
