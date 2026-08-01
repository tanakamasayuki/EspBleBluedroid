# HfpHandsFree

HFP Hands-Free roleのSLC/SCO sessionと、Core内蔵CVSD/mSBC codecが変換した
mono 16-bit PCM callbackを示します。接続先はInquiry等で得たAudio Gatewayの
Classic addressを`connect()`へ渡してください。

`onPcmData()`と`onPcmRequested()`はHFP stack task上で同期実行されます。bufferを保持せず、
blockしないbounded queueへcopyしてください。制御callbackは`update()`から配送されます。

電話の発着信・応答などのcall-control APIは整備中です。
