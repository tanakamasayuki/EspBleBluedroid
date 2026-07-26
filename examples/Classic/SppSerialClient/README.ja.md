# Classic SPP Serial Client

> English: [README.md](README.md)

`EspBluedroidSppSerial`を使い、outgoing SPP Client sessionとボードの`Serial`を
双方向中継します。

```cpp
EspBleBluedroid bluetooth;
EspBluedroidSppSerial sppSerial(bluetooth);
```

未接続時にSerial Monitorへ接続先のClassic addressを入力すると、既存の非同期
`classic().spp().connect()`で接続を開始します。session成立後はwrapperが自動的に
利用可能になり、切断時は利用不可になります。再接続で発行される新しいsession IDにも
自動追従します。

対象はBluetooth Classicを搭載する無印ESP32とArduino-ESP32 3.3.11です。PSRAMは不要です。
