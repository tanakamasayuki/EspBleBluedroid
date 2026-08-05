# KeyboardHost

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

HID Host（Central）としてBLE keyboardへ接続します。HID Service `0x1812` をscanし、Pairing後にHID ReportをDiscoveryしてキーイベントを表示します。単一の `hidHost()` オブジェクトがmouse / consumer control / system control / gamepad Reportも配送するため、市販のBLE keyboardでも、2台目のボードで動かす[KeyboardDevice](../KeyboardDevice/)・[KeyboardNkro](../KeyboardNkro/)・[CompositeKeyboardMouse](../CompositeKeyboardMouse/) exampleでも使えます。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（HID Host / Central）
- BLE HID機器 × 1: 市販keyboard、またはKeyboardDevice / KeyboardNkro / CompositeKeyboardMouseを動かす2台目のボード

## 動作

- HID Service `1812` をadvertiseする最初のconnectableな機器をscanして接続します
- Bondingつきでsecurityを有効化し、HID Discoveryは`onSecurityChanged`の成功**後**に開始します — 市販keyboardは暗号化前のHID属性アクセスを拒否するためです
- layoutをEN-USに設定し、Discovery結果（Report ID、Battery）、raw usage snapshot、layout変換したASCII値つきのキーpressイベントを表示します
- 同じ `hidHost()` からmouse、consumer control、system control、gamepadのイベントも表示します
- `c` でCaps Lock LEDを点灯、`0` で全LED消灯（接続中のみ）

## 主なAPI

- `bluetooth.hidHost().discover(connectionId)` — 明示的なHID Discovery。再接続のたびに新しいConnection IDで呼び直すか、`setAutoRediscover(true)`を有効にします
- `keyboard.onDiscovered(cb)` — `success`、`reportId`、`hasCountryCode` / `countryCode`、`hasOutputReport`、`hasBatteryLevel` / `batteryLevel`、`detail` を持つ `EspBleHidKeyboardHostDiscovery`
- `keyboard.onKeyboardState(cb)` — layout非依存の256-bit usage snapshot（`isDown()`、`wasPressed()`、`wasReleased()`、`modifiers`）
- `keyboard.setKeyboardLayout(EspBleKeyboardLayout::EnUs)` / `keyboard.onKeyboard(cb)` — usage単位のpress/releaseイベント。`ascii` は変換可能な場合のみ非0
- `keyboard.onMouse()` / `onConsumerControl()` / `onSystemControl()` / `onGamepad()` / `onVendorInput()` — 複合HIDの種別別イベント
- `keyboard.setKeyboardLeds(connectionId, num, caps, scroll)` — fire-and-forgetのLED書込み（Write Without Response）
- `keyboard.ready(connectionId)` — Discoveryが完了し、そのlinkのInput Reportが購読済みであること
- `keyboard.invalidInputReportCount()` / `droppedEventCount()` — 解釈できなかったreport数と、queueが溢れて捨てたevent数

## メモ

- **Discoveryは1回の呼び出しではなく列です。** この後端はlinkあたり同時に1つのCentral GATT操作しか許さないため、`discover()`はService、Report Map、各Report Reference descriptor、HID Information、Battery Level、そしてInput Reportごとの購読を1つずつ順に辿ります。`discover()`がtrueを返したのは「列が始まった」ことだけで、完了は`onDiscovered()`で待ちます。それまでそのlinkで独自のGATT操作を出さないでください。
- **失敗時は`lastErrorName()`ではなく`result.detail`を読みます。** `onDiscovered()`は列が終わった後に走るため、ライブラリのlastErrorはその後の出来事の値に移っています。
- **modifierはbitでもありusageでもあります。** Shift+Aは`A`のeventの`event.modifiers = 0x02`として届くのと同時に、usage `0xe1`・`ascii = 0`の単独eventとしても届きます。`isDown(0xe1)`もこれに答えます。
- **state eventが先です。** 1つのreportに対して`onKeyboardState()`がusage単位の`onKeyboard()`より先に走るので、sketchはedgeが報告される前にsnapshot全体を読めます。
- Discovery / state / keyイベントはすべて `bluetooth.update()` から配送されます。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| クラス・メソッド・callback名 | `ble.hidHost()` | 同じ |
| Discovery | GATT操作の列として発行 | APIは同じ。内部では1操作ずつ。この後端はlinkあたり1つのCentral GATT操作しか許さないため |
| keyboard eventの`event.rawData` / `rawLength` | 空のまま | decode元のreportを載せる（6KRO keyboardなら`length`は8）。EspBle側への要望は[docs/ESPBLE_FEEDBACK.ja.md](../../../docs/ESPBLE_FEEDBACK.ja.md) |
| 再接続の自動化 | `setAutoReconnect(true)` ＋ `persistentSubscriptions` ＋ `setAutoRediscover(true)` | `setAutoRediscover(true)`はあり、既知peerの再接続・再暗号化後にDiscoveryを再実行する。ただし**再接続自体はsketchの仕事**で、`setAutoReconnect()`は無い（手書きの型は[Gatt/Basics/AutoReconnectClient](../../Gatt/Basics/AutoReconnectClient/)） |
| 同時に扱えるkeyboard | 複数接続 | 同時に1link |

**移植方法:** ライブラリオブジェクトの宣言を変えるだけです。`setAutoReconnect()`に依存していた場合は[Gatt/Basics/AutoReconnectClient](../../Gatt/Basics/AutoReconnectClient/)の再接続ループを足します。HID側は`setAutoRediscover(true)`が引き受けます。

## 期待されるSerial出力

```
Keyboard ready: report=1 battery=73%
Keyboard state: modifiers=0x02 A=1 pressed=1 released=0
Key pressed: usage=0x04 ascii=0x41
Key pressed: usage=0xe1 ascii=0x00
```
