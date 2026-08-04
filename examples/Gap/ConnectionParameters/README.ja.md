# ConnectionParameters

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 2章「GAP編 — 探してつながる」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

確立済みの接続を調整する例です。

BLEでは、**応答性と消費電力を決めるパラメータを接続時に指定できません**。接続はコントローラが決めた値で成立し、そのあとで変更を要求します。この非対称さが分かりにくいところなので、このexampleは「接続直後に何が決まっていたか」を表示してから変更します。

## 3つのパラメータ

| パラメータ | 意味 | 単位 |
|---|---|---|
| **Connection Interval** | 通信機会の周期。短いほど応答が速く、電力を食う | 1.25 ms |
| **Peripheral Latency** | 送るものがないときPeripheralが応答をスキップしてよい回数 | 回数 |
| **Supervision Timeout** | この時間だけ通信が途絶えたら切断とみなす | 10 ms |

単位はBLE仕様そのままの生の値です。`interval = 24` は 24 × 1.25 = 30 ミリ秒を意味します。

**Supervision Timeoutには制約があります。** `(1 + latency) × maxInterval × 2` より長くする必要があります。Latencyを増やすとPeripheralが長く沈黙しうるため、それを切断と誤判定しないためです。この条件を満たさない要求は相手に拒否されます。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（Central）
- 接続先のPeripheral — 2台目のボードで[Gap/Advertise](../Advertise/)、またはHID Service（`0x1812`）をadvertiseする任意の機器

## 動作

- Service UUID `0x1812` をadvertiseする相手を探して接続します
- 接続直後に、**コントローラが決めた**interval / latency / timeoutを表示します
- `f` で低遅延profile（interval 15〜30 ms、latency 0）、`s` で省電力profile（interval 400〜500 ms、latency 4）を要求します
- `d` で切断します

## 主なAPI

- `bluetooth.updateConnectionParameters(id, minInterval, maxInterval, latency, timeout)` — 変更を要求する
- `bluetooth.onConnectionParametersUpdated(callback)` — 交渉の結果を受け取る
- `EspBleConnection` — `connectionInterval` / `peripheralLatency` / `supervisionTimeout`

## 注意

- **要求の戻り値は「受け付けたか」だけです。** 実際に何になったかは必ずコールバックで確認してください。相手が要求と違う値を返すことも、拒否することもあります。
- **どちらの役割からでも要求できます。** ただし最終的に決めるのはCentral側のコントローラです。Peripheralからの要求はCentralが承認して初めて反映されます。
- **調整できるのはCentral側の接続だけです。** `updateConnectionParameters()` には、この機器が `connect()` で開いたlinkのIDを渡します。相手が決めた値そのものは、どちらの側でもconnection snapshotで確認できます。
- **接続時にパラメータを指定することはできません。** `connect()` にパラメータ引数はないため、既定値が用途に合わない場合は接続してから `onConnected()` の中で変更します。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| PHYの変更 | `updatePhy()` / `onPhyUpdated()` / `EspBleConnection::txPhy`・`rxPhy` | 提供しない |
| 接続時のパラメータ指定 | 提供しない | 提供しない |

**なぜ違うのか:** 無印ESP32の無線はBluetooth 4.2 LEまでで、LE 2M PHYもLE Coded PHYも持ちません。切り替える先が存在しないため、このライブラリは `updatePhy()`・`onPhyUpdated()`・`txPhy`／`rxPhy`のsnapshot fieldをそもそも公開していません。「受け付けたが永久に反映されない要求」を作らないためです。

**移植のしかた:** EspBle版からPHYの分岐とコールバックを削除するだけです。単位、Supervision Timeoutの制約、要求と結果の非対称さといった残りの内容はEspBleと同じです。

## 期待されるSerial出力

```
Scanning for a peripheral...
CONNECTED interval=40 (50.00 ms) latency=0 timeout=256 (2560 ms)
Commands: f fast, s slow, d disconnect
REQUEST slow accepted=1
PARAMETERS interval=400 (500.00 ms) latency=4 timeout=600 (6000 ms)
REQUEST fast accepted=1
PARAMETERS interval=24 (30.00 ms) latency=0 timeout=400 (4000 ms)
```
