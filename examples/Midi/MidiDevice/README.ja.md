# MidiDevice

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

標準のBLE MIDI Serviceを使ってBLE MIDI PeripheralをAdvertiseします。Serial入力でNote On/Offを送信し、接続したHostから届くMIDIを表示します。[MidiHost](../MidiHost/) exampleや一般的なBLE MIDI Host（スマホ/タブレットのDAW等）と接続できます。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（MIDI Device / Peripheral）
- BLE MIDI Host × 1: スマホ/タブレットのDAW、PC、または[MidiHost](../MidiHost/) exampleを動かす2台目

## 動作

- `begin()`の前にBLE MIDI ServiceとI/O characteristicを登録します（Service UUIDはAdvertisingへ追加）
- Serialコマンド`n`で中央ハのNote On → Note Offを送信します（Hostが購読中のときのみ）
- Hostから届いたMIDI（Host → Device）をSysExチャンクも含めて表示します

## helperはcallbackを奪いません

`EspBleMidiDevice`はGATT Serverのwritten / subscriptionChanged / sentイベントを必要としますが、単一の`on*()`ではなく`add*Listener()`で登録します。そのためsketch側は同じイベントに対して自分の`onWritten()`や別の`addWrittenListener()`をそのまま使えます — helperとアプリケーションが並んで同じイベントを観測します。この順序は`tests/peer/multi_listener`が実機で固定しています。

## 主なAPI

- `EspBleMidiDevice midi(bluetooth)` — `EspBleBluedroid`インスタンスへの参照で構築
- `midi.begin()` — Serviceを登録。`bluetooth.begin()`より前に呼ぶ
- `midi.noteOn(channel, note, velocity)` / `midi.noteOff(...)` — channel voiceメッセージを送信
- `midi.controlChange()` / `programChange()` / `polyPressure()` / `channelPressure()` / `pitchBend()`
- `midi.sendSysEx(data, length)` / `midi.sendingSysEx()` — 長いSysExはpacketへ分割され、`onSent`ごとに1つ送信されます
- `midi.onMessage(callback)` — Hostから届いたMIDIを`EspBleMidiMessage`へデコード
- `midi.ready()` — Hostが購読中はtrue

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| クラス名・メソッド名・callback名 | `EspBleMidiDevice(EspBle &)` | `EspBleMidiDevice(EspBleBluedroid &)`以外は同一 |
| 購読中のHost | 最大4 | 1 — GATT Serverが公開するPeripheralリンクは1本 |
| ワイヤ形式 | `EspBleMidi.h`のBLE MIDI 1.0 packet | 同一ファイル・同一バイト列（`tests/unit/midi`） |

**なぜ:** `src/EspBleMidiProfile.h`はEspBleのファイルのライブラリ参照の型だけを差し替えたものです。2つをdiffすれば置換以外に違いがないことが分かります。購読数の上限はhelperではなくBluedroid GATT Server側の制約で、表の`MaxSubscribers`は4のままです。

**移植方法:** ライブラリオブジェクトの宣言を変えるだけです。MIDI sketchの他の部分は変わりません。

## 期待されるSerial出力

```
MIDI in: status=0xb0 data1=7 data2=100 ts=1234
SysEx chunk: start=1 end=0 length=16
```
