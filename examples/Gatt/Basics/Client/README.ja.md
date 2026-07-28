# Client

> English: [README.md](README.md)

独自Serviceを公開するPeripheralへ接続し、Centralの基本的なGATT Clientフローを
一通り実行します。

## 必要なもの

- 無印ESP32 × 1（このsketch。Central / GATT Client）
- 次のGATT databaseを公開し、Service UUIDをAdvertisingするPeripheral

| 属性 | UUID | 必要なproperty |
|---|---|---|
| Service | `10da4dd0-8eaa-4c69-9003-676174747277` | — |
| Characteristic | `10da4dd1-8eaa-4c69-9003-676174747277` | Read、Write、Write Without Response |
| Descriptor | `10da4dd2-8eaa-4c69-9003-676174747277` | Read、Write |

汎用GATT Serverアプリや別のfirmwareでこのdatabaseを用意できます。

## 動作

- Service UUIDをscanして接続します
- Service、Characteristic、Descriptorをconnection単位のsnapshotへ一覧Discoveryします
- snapshotから既知UUIDのCharacteristic handleを選び、Readします
- 応答ありWrite、応答なしWrite、Descriptor Read/Writeを順番に実行します

## 主なAPI

- `discoverServices()` / `onServicesDiscovered()` — database一覧Discovery
- `discoveredService*()` / `discoveredCharacteristic*()` /
  `discoveredDescriptor*()` — connection単位snapshotの照会
- handle指定`readCharacteristic()` / `writeCharacteristic()`
- `readDescriptor()` / `writeDescriptor()`と各完了callback

Central GATT操作は同時1件なので、次の要求は前の完了callbackから発行します。
要求APIの戻り値は受付結果で、完了は`update()`から後で配送されます。

## 期待されるSerial出力

```text
Services: 1, characteristics: 1, descriptors: 1
Read: ready
Descriptor: value description
Descriptor write complete
```
