# ServiceData

接続しないbroadcasterから、Service UUIDで意味づけたbinary値を放送する例です。

Environmental Sensing Service（`0x181a`）の温度値を5秒ごとに更新します。同じUUIDの
Service Dataを差し替え、Legacy Advertisingを再開します。

受信側は`EspBleScanResult::serviceDataFor()`で値を取得できます。変化を継続して
受け取る場合は`EspBleScanConfig::wantDuplicates = true`を指定してください。

Service Dataにも31 byte上限が適用されます。128 bit UUIDはUUIDだけで16 byte消費します。
