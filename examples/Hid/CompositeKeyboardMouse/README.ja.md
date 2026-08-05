# CompositeKeyboardMouse

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

keyboardとmouseを兼ねる1台のデバイスです。HOGPではHID Serviceは1つなので、両profileがそれを共有し、ReportはReport IDで区別されます。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（HIDデバイス / Peripheral）
- HID Host × 1: PC、スマホ、タブレット。OSのBluetooth設定からPairingします

## 動作

- 両方のprofileを`begin()`の前に構成します。Report Mapはその時点で登録済みのものから1度だけ合成されるためです
- Serialコマンド`h`で"hello"を入力、`m`でポインタを移動、`?`で各profileの`ready()`を表示します

## Service 1つ、Report Map 1つ

デバイスが公開するのはHID Service **1つ**、Report Map **1つ**（両方のdescriptorを含む）、そしてprofileごとのInput Report characteristicです。それらはすべてUUID `0x2A4D`で、Report Reference descriptor（`{report id, type}`）で区別されます。Hostは各NotificationをReport IDで振り分けます。`tests/peer/hid_composite`が、通知のみの5 profileすべてを同時に載せた状態で電波上の挙動を確認しています。

## 主なAPI

- `bluetooth.hidKeyboard().configure()` / `bluetooth.hidMouse().configure()` — どちらも`bluetooth.begin()`より**前**に
- `keyboard.write(text)` / `mouse.move(dx, dy)` — 各profileのReport
- `keyboard.ready()` / `mouse.ready()` — profileごと。Hostは各Input Reportを別々に購読するため

## 注意

- **`ready()`はprofileごとの問いです。** profileごとにCCCDがあるので、Hostがkeyboardだけ購読しmouseは購読していない状態があり得ます。使う直前のprofileを見てください。
- **`configure()`の呼び出し順は電波上に現れます**が、現れるのは属性の並び順だけです。Report Map内のdescriptorの順序は固定（keyboard、mouse、gamepad、consumer、system、vendor）で、Hostはそのどちらでもなく Report Referenceを使います。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| クラス名・メソッド名・callback名 | `ble.hidKeyboard()`、`ble.hidMouse()` | 同一 |
| Report Descriptor | 自前の表から生成 | 同一のバイト列（`tests/unit/hid_report_maps`が比較） |

**移植方法:** ライブラリオブジェクトの宣言を変えるだけです。

## 期待されるSerial出力

```
Send 'h' to type hello, 'm' to move the pointer.
ready: keyboard=1 mouse=1
```
