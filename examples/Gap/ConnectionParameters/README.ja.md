# ConnectionParameters

> English: [README.md](README.md)

確立済みBLE接続の応答性と消費電力を決めるパラメータを表示・変更します。

BLEの接続パラメータはcontroller間で交渉されます。要求を送れても、そのままの値が
採用されるとは限りません。このexampleは接続直後の現在値を表示し、変更要求のあとに
完了callbackから実際の合意値を確認します。

## 3つのパラメータ

| パラメータ | 意味 | 単位 |
|---|---|---|
| Connection Interval | 通信機会の周期。短いほど応答が速く、電力を使う | 1.25 ms |
| Peripheral Latency | Peripheralが通信機会をskipできる回数 | 回数 |
| Supervision Timeout | 通信が途絶えたと判断するまでの時間 | 10 ms |

`interval = 24`は30ms、`timeout = 400`は4秒です。

**Supervision Timeoutには制約があります。** `(1 + latency) × maxInterval × 2`
より長くする必要があります。Latencyを増やすとPeripheralが長く沈黙できるため、
その状態を切断と誤判定しない値にします。条件を満たさない要求は拒否されます。

## 必要なもの

- 無印ESP32 × 1（このsketch。Central）
- 接続先のPeripheral — 2台目のボードで[Gap/Advertise](../Advertise/)を動かすか、
  Battery Service（`0x180F`）をAdvertisingする任意の機器

## 動作

- Battery Service UUIDをAdvertisingする相手を探して接続します
- 接続直後にcontrollerが選んだ現在値を表示します
- `f`で15〜30ms・latency 0の低遅延profileを要求します
- `s`で400〜500ms・latency 4の省電力profileを要求します
- `d`で切断します

## 主なAPI

- `EspBleConnection::connectionInterval`
- `EspBleConnection::peripheralLatency`
- `EspBleConnection::supervisionTimeout`
- `updateConnectionParameters(id, min, max, latency, timeout)` — 更新要求
- `onConnectionParametersUpdated()` — 合意値を受け取るcallback

## 注意

- 更新APIの`true`は要求の受付を意味し、要求値への変更を保証しません。実際の合意値は
  必ずcallbackで確認してください。
- callbackのsnapshotは`connection()`から得る現在値にも同時に反映されています。
- このAPIは現在Central接続で利用できます。最終値はpeerとの交渉で決まります。
- 無印ESP32のcontrollerは1M PHYのみのため、このexampleはPHY変更を扱いません。

## 期待されるSerial出力

```text
CONNECTED interval=24 (30.00 ms) latency=0 timeout=400 (4000 ms)
Commands: f fast, s slow, d disconnect
REQUEST slow accepted=1
PARAMETERS interval=400 (500.00 ms) latency=4 timeout=600 (6000 ms)
```
