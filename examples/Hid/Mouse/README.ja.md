# Mouse

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

BLE HID mouse（HID over GATT / HOGP）です。相対移動のポインタにボタンとホイールを備え、HID Service `0x1812`をAdvertisingします。移動やクリックはSerialコマンドで発生させます。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（HID mouse / Peripheral）
- HID Host × 1: PC、スマホ、タブレット。OSのBluetooth設定からPairingします

## 動作

- `begin()`の前に`bluetooth.hidMouse().configure()`を呼びます。keyboardと同じHID・Battery・Device Information Serviceが合成され、Report Mapに入るのはmouseのdescriptorだけです
- ボタンを既定の5個ではなく3個で宣言します。個数はReport Descriptorへ埋め込まれ、Reportはいくつでも4 byteのままです
- Serialコマンド`m`で移動（+12, -8）、`c`でクリック、`w`でスクロール、`d`でドラッグします

## 主なAPI

- `bluetooth.hidMouse().configure(config)` — profileを登録。`bluetooth.begin()`より**前**に呼ぶ
- `mouse.move(dx, dy, wheel)` — 相対移動のReport 1件
- `mouse.press()` / `release()` / `releaseAll()` / `buttons()` — 押されているボタンの状態
- `mouse.click(ESP_BLE_HID_MOUSE_LEFT)` — 押して離す
- `mouse.wheel(amount)` — 移動せずスクロール
- `mouse.ready()` — Hostが接続し、（securityが有効なら）暗号化され、購読済み

## 注意

- **ドラッグは「ボタンを押したままの移動」です。** `move()`は押されているボタンをそのまま維持するので、ドラッグは`press()`→`move()`→`releaseAll()`です。Reportごとにボタン状態を指定し直す必要はありません。
- **値はすべて位置ではなく差分です。** HID Reportは符号付き8 bitの移動量なので、長い移動は1件の大きなReportではなく複数のReportになります。
- **securityは実質必須です。** 有効なら、deviceはHostの接続時点でPairingを要求し、加えてHID属性は暗号化なしのReadに「暗号化不足」エラーを返します（`tests/peer/hid_security`）。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| クラス名・メソッド名・callback名 | `ble.hidMouse()` | 同一 |
| Report Descriptor | 自前の表から生成 | 同一のバイト列（`tests/unit/hid_report_maps`が比較） |

**移植方法:** ライブラリオブジェクトの宣言を変えるだけです。

## 期待されるSerial出力

```
Send 'm' to move, 'c' to click, 'w' to scroll, 'd' to drag.
```
