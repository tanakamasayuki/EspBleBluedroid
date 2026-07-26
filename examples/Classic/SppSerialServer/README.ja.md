# Classic SPP Serial Server

> English: [README.md](README.md)

`EspBluedroidSppSerial`を使い、SPP Serverとボードの`Serial`を双方向中継します。

```cpp
EspBleBluedroid bluetooth;
EspBluedroidSppSerial sppSerial(bluetooth);
```

incoming sessionの成立後はArduinoの`Stream`/`Print`と同じ`available()`、`read()`、
`write()`、`print()`を利用できます。wrapperはactive sessionへ自動追従するため、
session IDの保存やbind処理は不要です。切断時は`connected()`がfalseになり、
次のincoming sessionが成立すると再び利用可能になります。

対象はBluetooth Classicを搭載する無印ESP32とArduino-ESP32 3.3.11です。PSRAMは不要です。
