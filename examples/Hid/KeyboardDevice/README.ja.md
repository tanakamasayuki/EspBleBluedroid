# KeyboardDevice

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

ボードをBLE HID keyboard（HID over GATT、固定6KRO）にします。PCやスマホからPairingすると普通のキーボードとして入力でき、キー入力はSerialコマンドで発生させます。HostからのLED Output Report（Num/Caps/Scroll Lock）も届いた時点で表示します。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（HID keyboard / Peripheral）
- HID Host × 1: PC、スマホ、タブレット。OSのBluetooth設定からPairingします

## 動作

- `begin()`の前にHID・Battery・Device Information Serviceを登録し、HID Service UUIDとkeyboard appearanceをAdvertisingへ追加します
- Serialコマンド`a`でShift+Aを入力、`r`で全キーreleaseします
- HostがLED状態を書き込むたびに表示し、Protocol Modeを切り替えるたびに表示します
- 切断のたびにAdvertisingを再開するので、Host側はbondから再接続できます

## 同一UUIDのReport characteristic 2件

HOGPはInput ReportとOutput Reportを**UUID 0x2A4Dを共有する**2つのCharacteristicとして置き、Report Reference descriptor（`{report id, type}`）で区別します。このprofileが「同一UUIDのCharacteristicを公開できるGATT Server」を必要とするのはこのためです。公開できることは`tests/peer/duplicate_uuid_server`が、Host側が実際に2つを区別できることは`tests/peer/hid_keyboard_device`が確認しています。

## 主なAPI

- `bluetooth.hidKeyboard()` — この機器のkeyboard profile
- `keyboard.configure(config)` — Serviceを登録。`bluetooth.begin()`より**前**に呼ぶ
- `keyboard.ready()` — Hostが接続し、（securityが有効なら）暗号化され、購読済み
- `keyboard.sendReport(report)` / `releaseAll()` — modifier 1 byteと最大6 usage
- `keyboard.pressKey(char)` / `tapKey(char)` / `write(text)` — 文字→usageの変換はlayoutが行う
- `configure()`前の`keyboard.enableNkro()` — N-key rollover。以降は`sendReport(EspBleHidKeyboardNkroReport)`
- `keyboard.onOutputReport(callback)` / `keyboard.ledState()` — HostのLED状態をeventとして、または問い合わせとして
- `keyboard.onProtocolMode(callback)` / `keyboard.protocolMode()` — ReportまたはBoot Protocol
- `keyboard.setBatteryLevel(level)` — Hostが読み、購読していればNotificationも送る

## 注意

- **`ready()`は「接続済み」ではありません。** HOGP Hostはdescriptorを読み、Pairingし、その後にInput Reportを購読します。購読までは`sendReport()`が`InvalidState`で失敗し、detailがどちらの状態かを示します（`no connected HID Host` / `no subscribed HID Host`）。送信結果から接続状態を推測せず、`ready()`を見てください。
- **securityは実質必須です。** `security.enabled`が有効なら、このdeviceはHostが接続した時点でPairingを要求します（Peripheral側からのSecurity Request）。加えてHID属性は暗号化なしのReadに「暗号化不足」エラーを返します。`tests/peer/hid_security`がその両方と、Pairingを拒否したHostが何も得られないことを固定しています。`security.enabled`をoffにすると属性がそのまま読めてしまい、Pairingしないhostもあります。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| クラス名・メソッド名・callback名 | `ble.hidKeyboard()` | 同一 |
| Report Descriptor | 自前の表から生成 | 同一のバイト列（`tests/unit/hid_report_maps`が比較） |
| `onOutputReport()`と`ledState()` | 状態はHostのwrite時点で更新、callbackは1 update()遅れる場合がある | GATT Serverが元々writeを`update()`から配送するため、両方が同じ`update()`で起きる |
| 利用できるprofile | keyboard、mouse、consumer、system、gamepad、vendor、custom、host | 同じ。Host側もある（[KeyboardHost](../KeyboardHost/)） |

**移植方法:** ライブラリオブジェクトの宣言を変えるだけです。keyboard sketchの他の部分は変わりません。

## 期待されるSerial出力

```
Send 'a' to type Shift+A and 'r' to release all keys.
Protocol Mode: Report
Keyboard LEDs: num=0 caps=1 scroll=0
```
