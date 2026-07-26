# Classic SPP Stream

> English: [README.md](README.md)

確立済みSPP sessionをArduino `Stream`実装の`EspBluedroidSppStream`で包む
exampleです。`attach()`後は毎回session IDを渡さず、通常の`available()`、
`peek()`、`read()`、`write()`、`print()`、`println()`を利用できます。
constructorへsessionを直接渡すこともできます。

ラッパーはBluetooth stackやsessionを所有しません。bindしたsessionが切断されると
`connected()`とbool変換はfalseになります。`attach()`の再呼出しはbind先を置き換え、
`detach()`はbindを解除します。`availableForWrite()`は固定長送信queueの残容量を返し、
大きな`write()`はbackend上限の990 byte単位へ分割して、queueが満杯になるまでに
受理したbyte数を返します。
`flush()`はqueueの完了またはsession切断まで待機します。

このexampleは認証なしのSPP Serverを開始して受信streamをechoします。対象は
無印ESP32で、PSRAMは不要です。
