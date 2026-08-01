# IBeacon

> English: [README.md](README.md)

Apple iBeaconをnon-connectable・non-scannableなAdvertisingとして送信します。
payloadはEspBleと共通のbackend非依存`EspBleIBeacon.h`で構築します。

`EspBleIBeaconData`へproximity UUID、major、minor、1 m地点の校正RSSIである
`measuredPower`を設定し、`espBleEncodeIBeacon()`で25 byteのManufacturer Dataへ
変換します。受信側では`espBleIsIBeacon()`と`espBleDecodeIBeacon()`を使えます。

無印ESP32 1台と、[Scan](../Scan/)または一般的なbeacon scannerで確認できます。
