# HfpAudioGateway

HFP Audio Gateway roleを開始し、Hands-Freeからの着信SLCと双方向mono 16-bit PCMを扱います。
開始時にClassicをconnectable/discoverableへ設定します。

stock Arduino-ESP32ではCore内蔵CVSD/mSBC codecにつながるlegacy PCM経路を使います。
PCM callbackはstack task上なので、USB Audio等へのbridgeは別library側のbounded queueを
介してください。call indicatorや発着信制御の公開APIは整備中です。
