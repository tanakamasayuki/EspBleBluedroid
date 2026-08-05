# CustomClient

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

**汎用GATTクライアント**（Central）でCustom HIDデバイスの任意Report Descriptorを読み、Reportを駆動します。[CustomDevice](../CustomDevice/) exampleとペアです。

HIDデバイスは同一UUID `0x2A4D` のReport characteristicを複数持つため、対象はすべて個別の **attribute handle** で指定します。各Reportの役割は、**Report Reference descriptor**（`0x2908`、report ID 1byte＋type 1byte: 1=Input / 2=Output / 3=Feature）から読みます——HIDが本来そう宣言しているからです。そのdescriptorの指定もhandleで行います。Report Referenceはどれも「`0x2A4D` のcharacteristicの下の `0x2908`」なので、Service/Characteristic/Descriptor UUIDの組では**全部に一致してどれにも特定できません**。

動くHID Hostが欲しいだけなら、[KeyboardHost](../KeyboardHost/)が同じことを内部でやります。このexampleはその手作業版で、ライブラリがモデル化していないReportを持つデバイス向けです。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（Central / GATTクライアント）
- [CustomDevice](../CustomDevice/) を動かす無印ESP32 × 1（HID Device / Peripheral）

## 動作

- HID Service（`0x1812`）をadvertiseするデバイスをactive scanで探して接続します
- 接続時にserviceをdiscoverし、完了後に各 `0x2A4D` characteristicを自分の `0x2908` descriptorと対応付けます。descriptorは1つのcharacteristicに属し、その紐付けは持ち主の値ハンドル（`EspBleGattDescriptorInfo::characteristicHandle`）です
- 各Report Referenceを**handle指定で**、1つずつ読みます。この後端はlinkあたり1つのCentral GATT操作しか実行しないため、次のReadは前のResultから発行します
- type byteで役割を決め、Input Reportはhandleで購読し、Output Reportのhandleは書き込み用に保持します
- 2byteの入力Report（符号付きダイヤル差分＋ボタン）をデコードします
- `o` で1byteの出力Report（`0x02`、LED状態）をhandleで書き込み

## 主なAPI

- `bluetooth.discoverServices(connectionId)` / `bluetooth.onServicesDiscovered(cb)` — GATT Discoveryの起動と受信
- `bluetooth.discoveredCharacteristicCount(connectionId, serviceUuid)` / `bluetooth.discoveredCharacteristic(connectionId, index, info, serviceUuid)` — characteristicを列挙。`EspBleGattCharacteristicInfo` は `characteristicUuid`、`handle`、`notifiable`、`writable` を持つ
- `bluetooth.discoveredDescriptorCount(...)` / `bluetooth.discoveredDescriptor(...)` — descriptorを列挙。`EspBleGattDescriptorInfo` は `descriptorUuid`、`handle`、そして持ち主の `characteristicHandle` を持つ
- `bluetooth.readDescriptor(connectionId, descriptorHandle)` / `bluetooth.onDescriptorRead(cb)` — descriptorをattribute handleで読む。結果の `descriptorHandle` が読んだdescriptor、`handle` がそれを持つcharacteristic
- `bluetooth.subscribe(connectionId, handle, true)` — attribute handleで購読
- `bluetooth.onNotification(cb)` — 送信元 `handle` と `value` を持つ `EspBleGattNotification`
- `bluetooth.writeCharacteristic(connectionId, handle, data, length, response)` — handleで書込み

## メモ

- **1link 1操作なので、順序制御はsketchの仕事です。** `readNextReference()` が前のResultから次のReadを出し、Readが尽きてから購読へ進みます。callbackの中から次の操作を出して問題ないのは、callbackが後端のcallback内ではなく `bluetooth.update()` から配送されるためです。
- **失敗しても進めます。** 失敗したReadでもcursorを進めないと、読めないdescriptor 1件で後続すべてが止まります。
- CustomDeviceはsecurity有効で動作するため、bondingしないクライアントは拒否される場合があります。暗号化なしで試すにはデバイス側のsecurityを無効化する（またはこのクライアントにbondingを追加する）ようにしてください。
- discoverされるUUIDは128-bit形式（`0000XXXX-...`）で返るため、sketchは16-bit短縮形とどちらでも一致させます。
- UUID指定の `readDescriptor(connectionId, serviceUuid, characteristicUuid, descriptorUuid)` も用意されており、characteristicのUUIDが一意なときはそちらが素直です。ここでは使えません——最初に見つかった `0x2A4D` に一致してしまい、それが目的のReportとは限らないためです。
- propertyから推測せずtypeを読むのは、**Output ReportもFeature Reportもwritable**で区別できないからです。propertyにも意味はあります（Write Without Responseを持つのはOutputだけ。Featureは設定なので必ず応答付き書き込みになる）が、デバイスが実際に宣言しているのはtype byteです。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| クラス・メソッド名 | `ble.readDescriptor()` | 同じ |
| 複数のReadの発行 | 自動でキューへ積まれ順に実行されるため、`onServicesDiscovered` からまとめて発行できる | **1link 1操作**。1件目の実行中に2件目を出すと失敗する。数珠つなぎにする——このsketchはhandleを配列に持ち、次の1件を `onDescriptorRead` から読む |
| 長いdescriptor / characteristic値 | 全体が返る | 同じ。Bluedroidが内部でReadを継続する（`tests/peer/long_value`） |
| 同時に扱えるデバイス | 複数接続 | 同時に1link |

**移植方法:** ライブラリオブジェクトの宣言を変えたうえで、まとめて出していたGATT呼び出しを数珠つなぎに直します。このexampleで必要だった変更はそれだけです。

## 期待されるSerial出力

```
Scanning for a Custom HID device. Send 'o' to write the output LED report.
Reading 2 Report Reference descriptors
Input report: id=1 handle=42
Output report: id=1 handle=45
Input report: dial delta=5 buttons=1
```
