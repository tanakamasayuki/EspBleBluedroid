# Server

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../../DIFFERENCES_FROM_ESPBLE.ja.md)

Read/Write可能なCharacteristicとDescriptorを1つずつ持つ独自GATT Serviceを登録し、advertiseします。Characteristicは応答あり/なし両方のWriteに対応します。あわせて、**読まれた瞬間に値を作る**Characteristicも1つ持ちます。

2台目のボードで[Gatt/Basics/Client](../Client/) example（同じUUIDを対象にしています）を動かすか、nRF Connectなどの汎用GATT Clientアプリから操作できます。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（Peripheral / GATT Server）
- GATT Client × 1（Gatt/Basics/Clientを動かす2台目のボード、またはスマートフォンアプリ）

## 動作

- `begin()`前にService `10da4dd0-…`、Characteristic `10da4dd1-…`、Descriptor `10da4dd2-…`、Read専用の`10da4dd3-…`を登録します
- 初期値を`ready`に設定します
- Clientからの書込みをConnection IDと一緒に表示します
- `10da4dd3-…`が読まれたときは、その場で `millis()` を値にして返します
- ClientがみつけられるようにService UUIDをadvertiseします

## ハンドルで組み立てる

登録は**3段のハンドル連鎖**になります。`addService()` が返すハンドルを `addCharacteristic()` に渡し、それが返すハンドルを `addDescriptor()` に渡す、という形です。

```cpp
const EspBleGattService service = gattServer.addService(SERVICE_UUID);
characteristic = gattServer.addCharacteristic(service, CHARACTERISTIC_UUID, valueConfig);
descriptor = gattServer.addDescriptor(characteristic, DESCRIPTOR_UUID, descriptorConfig);
```

以降の値設定・送信・イベント判定はすべてこのハンドルで行い、UUIDでは指定しません。**UUIDは「型」であって「どれか」を表さない**ためです。仕様上、1台が同じUUIDのServiceを複数持てますし、Client側から見れば同じUUIDのCharacteristicが並ぶ相手（HIDのReportなど）も普通にあります。

ハンドルはグローバル変数などに保持してください。失敗すると無効なハンドルが返るので、`valid()` で判定できます。

## 読まれた瞬間に値を作る

`setValue()` で先に値を置いておく形は、値が変わったときにこちらが更新できる場合に向きます。センサーのように「読まれた時点の値」を返したい場合は、`onRead()` を使います。

```cpp
gattServer.onRead([](const EspBleGattReadRequest &request) {
  if (request.characteristic != liveCharacteristic) return;
  bluetooth.gattServer().setValue(liveCharacteristic, String(millis()));
});
```

コールバックの中で `setValue()` した値が、そのまま相手へ返ります。定期的に `setValue()` を呼び続ける必要がなくなり、**誰も読まないなら値を作る処理自体が走りません**。

**このコールバックだけは `update()` ではなくBLEスタックのタスクで走ります。** ATTの読み取り応答を返す前に値が必要で、後回しにできる場所がないためです。したがって次の2点に注意してください。

- **短く保つこと。** ここで待たせるとスタック全体が止まり、相手からは読み取りのタイムアウトに見えます。Serial出力もこの中では避けてください
- **`loop()` と同時に走ります。** 共有変数を触るなら、他のコールバックと違って排他制御が必要です

## 主なAPI

- `bluetooth.gattServer().addService(uuid)` — Serviceを登録してハンドルを返す。`begin()`前に呼ぶ必要があります
- `addCharacteristic(service, uuid, config)` — Serviceのハンドルを渡してCharacteristicを登録し、そのハンドルを返します
- `EspBleGattCharacteristicConfig` — `readable`、`writable`のほか`notifiable`、`indicatable`、暗号化/認証permission
- `addDescriptor(characteristic, uuid, config)` / `EspBleGattDescriptorConfig` / `setDescriptorValue(descriptor, value)` — Descriptor定義、permission、binary-safeな値
- `gattServer.setValue(characteristic, value)` / `gattServer.value(characteristic, out)` — 保持値（binary-safeな`String`。pointer+length overloadもあります）
- `gattServer.onWritten(callback)` — `connectionId`、書き込まれたCharacteristicのハンドル、値を持つ`EspBleGattWrite`
- `gattServer.onRead(callback)` — 読み取り要求。`EspBleGattReadRequest`は`connectionId`と対象のハンドルを持ちます
- `gattServer.onDescriptorWritten(callback)` — Descriptorのハンドルと値を持つ`EspBleGattDescriptorWrite`

## 注意

- **コールバックは全Characteristic共通です。** 複数登録している場合は `write.characteristic == myHandle` で対象を判定してください。イベントにはUUID文字列も入っていますが、同じUUIDが複数あると区別できないため、ハンドルで比べるのが確実です。
- **同じService内に同じUUIDのCharacteristicを2つ置けます。** 仕様上も許され（HIDのReportがその典型）、`addCharacteristic()`は呼び出しごとに専用のhandleを返し、以降の操作はそのhandleで指定するため曖昧になりません。実際に両方が公開されていることは`tests/peer/duplicate_uuid_server`がpeerから読み出して確認しています。**ただし1つのCharacteristicに同じUUIDのDescriptorを2つは置けません**。Descriptorは所属Characteristic内でUUIDで引くため、2つ目に到達できず`addDescriptor()`が`InvalidArgument`で失敗します。
- **イベントごとのコールバックは1つで、リストではありません。** `onWritten()`、`onRead()`、`onSubscriptionChanged()`、`onSent()`はそれぞれ1つのコールバックを保持し、再度呼ぶと差し替わります。複数箇所へ配りたい場合は自分のコールバックの中で分配してください。
- **1回のATT応答に収まらない値は、複数回の応答で読まれます。** Clientが続きを要求し、このライブラリのGATT Clientも全体を取得します（[Gatt/Basics/Client](../Client/)）。続きを要求しない相手には先頭の`mtu - 1` byteだけが見えますが、それはClient側の判断でServer側の制限ではありません。
- 登録はすべて `begin()` より前に行う必要があります。`begin()` 後の `addService()` は `InvalidState` で失敗します。
- **databaseには上限があります。** Service 8、Characteristic 32、Descriptor 16です。超えた登録呼び出しは`ResourceExhausted`で失敗します。
- **相手が切断してもadvertisingは自動的には再開しません。** Bluedroidは接続成立時にadvertisingを止め、こちらにはPeripheral側の切断eventがないため、sketchが再度`advertising().start()`を呼ばない限り、このServerは1 bootで1回しか接続を受け付けません。再開の方法は[DIFFERENCES_FROM_ESPBLE.ja.md](../../../DIFFERENCES_FROM_ESPBLE.ja.md)にまとめています。
- **`write.connectionId`は、そのイベントが来たPeripheral linkを表します。** これはGATT Server側のIDです。`bluetooth.connection(id, out)`はCentral linkのみを表すので、両者を混ぜないでください。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| databaseの上限 | Service 8 / Characteristic 32 / Descriptor 16 | 同じ |
| 同一Service内の同一UUID | 可能 | **登録時に拒否** |
| 1イベントに複数リスナ | `add*Listener()`（`MaxListenersPerEvent`まで） | イベントごとに1つ |
| `onRead()`のcontext | BLE stack task | BLE stack task（同じ） |
| Peripheral connection snapshot | 着信linkでも`bluetooth.connection(id, out)`が使える | **なし**。`write.connectionId`とGATT Serverのイベントを使う |

**なぜ違うのか:** GATT Serverの属性テーブルは現在もArduino-ESP32のBluedroid wrapper経由で構築しており、そのService内検索はUUIDで行われます。同じUUIDの2つ目は名指しできないため、実行時に取り違えるのではなく登録時に拒否します。コールバックが単一であることとPeripheral snapshotが無いことは現時点のスコープで、[docs/STATUS.ja.md](../../../../docs/STATUS.ja.md)に記載しています。

**移植のしかた:** 同一UUIDのCharacteristicはUUIDを分け、`add*Listener()`は1つのコールバックから分配する形へ置き換えます。`addService()`／`addCharacteristic()`／`addDescriptor()`／`setValue()`／`notify()`／`indicate()`の使い方はEspBleと同じです。

## 期待されるSerial出力

```
Connection 1 wrote: hello from Central
Descriptor 10da4dd2-8eaa-4c69-9003-676174747277 wrote: descriptor value
```
