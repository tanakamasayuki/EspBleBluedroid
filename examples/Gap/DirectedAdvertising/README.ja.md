# DirectedAdvertising

> English: [README.md](README.md)

既知のCentral 1台だけへ接続可能なAdvertisingを送るPeripheral側の例です。

通常のAdvertisingが周囲全体へ接続を募集するのに対し、Directed Advertisingは宛先addressをpacketへ入れます。宛先以外のcontrollerはpacketを破棄します。bond済み機器との高速な再接続に向いています。

## 必要なもの

- このsketchを動かす無印ESP32 × 1
- 宛先となるCentral

`TARGET_CENTRAL`をCentralのidentity addressへ書き換え、address typeも`Public`または`Random`へ合わせます。Central側の無印ESP32では`bluetooth.localAddress()`でpublic addressを確認できます。

## High DutyとLow Duty

| mode | interval | 継続時間 | 主な用途 |
|---|---:|---:|---|
| `highDuty = true` | 3.75 ms固定 | 最大1.28秒 | 切断直後の高速再接続 |
| `highDuty = false` | `setInterval()`の値。未指定時1.28秒 | `stop()`または接続まで | 消費電力を抑えた待機 |

High Dutyはtargetが接続しなくてもcontrollerが自動停止します。`bluetooth.update()`を継続して呼ぶと`isAdvertising()`も停止状態へ更新されます。

## 重要な制約

Directed Advertisingは仕様上、Local Name、Service UUID、Manufacturer DataなどのAD dataとScan Responseを一切送れません。`setDirectedTarget()`で宛先を設定している間は、通常Advertising用に構成したpayloadは送信されません。`clearDirectedTarget()`を呼ぶと同じAdvertising objectを通常送信へ戻せます。

`start()`は動作中のAdvertisingを停止して、新しい設定で開始します。

RPAを使うbond済みpeerを指定する場合は、一時的に観測したRPAではなくidentity addressと正しいidentity address typeを指定します。

Directed Advertisingは接続先を限定しますが、接続後の暗号化や認証を保証しません。値を守る場合はBLE Securityも使用してください。

## 操作

- `h` — High Dutyで再開
- `l` — Low Dutyで再開
- `x` — 停止

## 主なAPI

```cpp
auto &advertising = bluetooth.advertising();
advertising.setDirectedTarget(
  targetAddress, EspBleAddressType::Public, true);
advertising.start();
```
