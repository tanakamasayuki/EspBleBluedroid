# SPP接続中のBLE Scan

> English: [README.md](README.md)

Classic SPP Serverのaddressを入力します。SPP session成立後、Classic sessionを
維持したまま10秒間のactive BLE Scanを実行します。

root objectは1つのdual-mode Bluedroid stackを所有しますが、BLE ScanとClassic SPPは
別のAPI・result型のままです。両方のevent配送には`update()`を呼び続けます。

2台自動テストではBLE Scan callbackから開始したbinary SPP trafficも確認しています。
BLE GATTとSPPの同時trafficは今後の別test sliceです。
