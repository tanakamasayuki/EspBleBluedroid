# Client

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../../DIFFERENCES_FROM_ESPBLE.ja.md)

[Gatt/Basics/Server](../Server/) exampleへ接続し、CentralのGATT Clientフローを一通り実行します: database一覧Discovery → 既知UUIDのDiscovery → Read → 応答あり/なしWrite → Descriptor Read/Write → 要求時に作られる値のRead。各要求は直ちに`bool`を返し、完了は`bluetooth.update()`からのイベントとして後から届きます。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（Central / GATT Client）
- [Gatt/Basics/Server](../Server/) exampleを動かす無印ESP32 × 1

## 動作

- ServerのService UUIDをscanして接続します
- Service、Characteristic、Descriptorを接続単位のsnapshotへ一覧Discoveryします
- 既知CharacteristicのDiscovery後、Read、応答あり/なしWrite、Descriptor Read/Writeを連鎖します
- 最後に、Server側が `onRead()` でその場で作る値（`10da4dd3-…`）を読み、`Live:` として表示します
- Central GATT操作が同時1件であることを示します — 次の操作は前の操作の完了callbackから発行します

## 連鎖として書く

GATT操作はすべて非同期で、しかも**Central側の操作は同時に1件しか実行できません**。実行中に2つ目を要求すると、その場で `InvalidState` として同期的に失敗します。つまり手続きを上から並べて書くことはできず、**「頼む → 完了イベントの中で次を頼む」の連鎖**になります。

このexampleはその連鎖がそのまま形に出ています。

```
onConnected        → discoverServices()
onServicesDiscovered → discoverCharacteristic()
onCharacteristicDiscovered → readCharacteristic()
onCharacteristicRead → writeCharacteristic()
onCharacteristicWritten → （応答なしWrite）→ readDescriptor()
onDescriptorRead   → writeDescriptor()
onDescriptorWritten → discoverCharacteristic(live) → readCharacteristic(live)
```

**イベントは操作の種類ごとに1つで、対象ごとには分かれません。** 複数のCharacteristicを扱うと同じcallbackへ順番に結果が届くので、`result.characteristicUuid` か、区別できない場合はハンドルで対象を判定します。このexampleは2つのCharacteristicを読むため、`onCharacteristicRead` の中で分岐しています。

一覧Discoveryの結果は**接続ごとのsnapshot**として保持され、切断するか次の一覧Discoveryを行うまで有効です。`discoveredService*()` などはそのsnapshotへの照会で、無線を使いません。

## 主なAPI

- `bluetooth.discoverServices()` / `onServicesDiscovered()` — peer databaseの一覧Discovery
- `discoveredService*()` / `discoveredCharacteristic*()` / `discoveredDescriptor*()` — 切断または次の一覧Discoveryまでsnapshotを照会
- `bluetooth.discoverCharacteristic(connectionId, serviceUuid, characteristicUuid)` — 既知UUIDのDiscovery
- `bluetooth.onCharacteristicDiscovered(callback)` — `success`、property、`detail`を持つ`EspBleGattResult`
- `bluetooth.readCharacteristic(...)` / `bluetooth.onCharacteristicRead(callback)` — `result.value`が値を保持します（binary-safe）
- `bluetooth.writeCharacteristic(connectionId, serviceUuid, characteristicUuid, value, withResponse)` / `bluetooth.onCharacteristicWritten(callback)`
- `bluetooth.readDescriptor()` / `writeDescriptor()`と各完了callback
- 各操作の末尾の`timeoutMilliseconds`（既定10000、0は無効）— timeoutは`EspBleError::Timeout`で完了します
- Central GATT操作は排他です: 実行中に2つ目を要求すると`InvalidState`で同期的に失敗します

## 注意

- **Readは1回のATT応答で完結するため、長い値は切り詰められます。** `readCharacteristic()`はATT Readを1回だけ発行します。`mtu - 1` byteに収まらない値は、エラーなしで途中まで返ります。ここにRead Longの自動処理はありません。値がその長さを超えうる場合は、MTUを上げる（[Gap/Mtu](../../../Gap/Mtu/)）か、相手側でNotificationなどに分割してください。
- **Writeは分割されません。** 書き込みはATTの1回の要求として送られ、Long Write（複数回に分けて書く手続き）は行いません。読み取りと非対称なのは、分割の可否が相手側の実装にも依存するためです。1回に載る上限はMTU − 3バイトで、`maximumNotificationPayload()` と同じ値です。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| `mtu - 1`より長い値 | 自動的に分割Readして結合する（Read Long） | **ATT Read 1回のみ。値は切り詰められる** |
| GATT操作の同時実行 | 複数を続けて発行できる | 1接続につき同時1操作 |
| Discovery snapshotの上限 | Service 16 / Characteristic 48 / Descriptor 48 | 同じ |

**なぜ違うのか:** Read経路は`esp_ble_gattc_read_char()`（およびwrapperの単発Read fallback）を使い、どちらもRead Blobの連続要求を行いません。できるふりをせず、1回の応答が運んだ内容をそのまま返します。

**移植のしかた:** 値を`mtu - 1` byte以内に収める、MTUを大きく交渉する、またはアプリケーション側で分割します。呼び出し自体は変わりません。

## 期待されるSerial出力

先頭の一覧Discoveryの件数は相手のGATT database（backendが用意する標準Serviceを含む）によって変わり、`Live:` の値は読み取り時点の `millis()` です。

```
Services: ..., characteristics: ..., descriptors: ...
Read: ready
Descriptor: EspBleBluedroid value
Descriptor write complete
Live: 8421
```
