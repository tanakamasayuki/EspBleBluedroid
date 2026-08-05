# KeyboardNkro

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

N-key rolloverです。キーボード全体の状態を1 Report（modifier 1 byte ＋ 224 bitのusage bitmap）で送るため、boot互換Reportの6キー制限なく何キーでも同時に押せます。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（HID keyboard / Peripheral）
- HID Host × 1: PC、スマホ、タブレット。OSのBluetooth設定からPairingします

## 動作

- `configure()`より**前**に`enableNkro()`を呼びます。これがReport Mapに入るkeyboard descriptorを決めます
- MTUを上げます。29 byteのInput Reportには29 byteのATT payload、つまりMTU ≥ 32が必要です
- Serialコマンド`n`で8キー同時押し、`r`で全release

## 主なAPI

- `keyboard.enableNkro()` — `configure()`より前に。以降に呼んでも公開済みdescriptorは変わらない
- `keyboard.sendReport(EspBleHidKeyboardNkroReport)` — 状態全体を1 Notificationで
- `report.press(usage)` / `release(usage)` / `isDown(usage)` / `clear()` — bitmapの操作。modifier usage（0xE0〜0xE7）はmodifier byteへ振り分けられる
- `keyboard.heldState()` — Hostへ最後に伝えた状態
- `keyboard.pressUsage()` / `releaseUsage()` — その状態への差分操作

## 注意

- **6キーReportはNKRO Reportではありません。** `sendReport(EspBleHidKeyboardReport)`は`keys[6]`しか持たないため、NKRO有効でも表現できるのは6 usageです（bitmapへ展開されNKRO形式で送られます）。7キー以上の同時押しには`EspBleHidKeyboardNkroReport`を組み立ててください。
- **1 Reportのほうが有利です。** `pressUsage()`でも8キー保持できますが、変化ごとに別Notificationになり接続間隔に律速されます。状態全体のReportなら1パケットです。
- **bitmapはusageの配列ではありません。** `press(0x04)`はbit 4を立てるのであって`bitmap[0] = 4`ではありません。modifier以外で`MaxBitmapUsage`（0xDF）を超えるusageは表現できず、`press()`はfalseを返します。
- **NKROとBoot Protocolは共存します。** Boot Protocol ModeのHostには固定8 byteのBoot Keyboard Reportがbitmapから変換されて届きます（保持キーが多すぎる場合はHIDのrolloverコード0x01になります）。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| クラス名・メソッド名・callback名 | `ble.hidKeyboard()` | 同一 |
| NKRO descriptorとReport形式 | 自前の29 byte形式 | 同一のバイト列（`tests/unit/hid_report_maps`が比較） |

**移植方法:** ライブラリオブジェクトの宣言を変えるだけです。

## 期待されるSerial出力

```
Send 'n' for eight simultaneous keys, 'r' to release all.
No subscribed HID Host yet.
```
