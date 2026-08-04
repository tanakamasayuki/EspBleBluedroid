# MidiHost

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)

CentralとしてBLE MIDI Peripheralへ接続します。BLE MIDI Serviceをscan → 接続 → Discovery → 購読 → デコード済みMIDIを表示。Serial入力でノートを送信します。[MidiDevice](../MidiDevice/) exampleや市販BLE MIDI楽器と接続できます。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（MIDI Host / Central）
- BLE MIDI Peripheral × 1: [MidiDevice](../MidiDevice/) exampleまたは市販BLE MIDI楽器

## 動作

- BLE MIDI Serviceをscanし、最初のconnectableな相手へ接続します
- 接続直後にMIDI I/O characteristicをDiscovery・購読します（この例はsecurity無効）
- デコード済みMIDIをSysExチャンクも含めて表示します
- Serialコマンド`n`で中央ハのNote Onを送信し、Note Offはwrite完了から送信します

## 2つのノートを連続では送れません

このライブラリのCentral GATT操作は**同時に1件**です。そのため`sendNoteOn()`の直後に`sendNoteOff()`を呼ぶと、Note Onのwriteが実行中のためその場で失敗します。このexampleでは`pendingNoteOff`を立て、write完了イベントからNote Offを送ります。

この完了イベントには既に観測者がいます — `EspBleMidiHost`が複数packetのSysExを進めるために使っています。そこでsketch側は`onCharacteristicWritten()`ではなく`bluetooth.addCharacteristicWrittenListener()`で自分の観測を追加し、helperの動作をそのまま残します。

## 主なAPI

- `EspBleMidiHost midi(bluetooth)` — `EspBleBluedroid`インスタンスへの参照で構築
- `midi.begin()` — HostのGATTコールバックを設定。`bluetooth.begin()`の後に呼ぶ
- `midi.discover(connectionId)` — Discovery・購読（接続後／security完了後に呼ぶ）
- `midi.onMidiMessage(callback)` — デコード済み`EspBleMidiMessage`（status / data1 / data2 / timestamp）
- `midi.sendNoteOn(connectionId, ...)` / `sendNoteOff()` / `sendControlChange()` / `sendProgramChange()`
- `midi.sendSysEx(connectionId, data, length)` / `midi.sendingSysEx()` — writeへ分割し、完了ごとに1つ送信
- `midi.ready(connectionId)` — 購読完了後はtrue
- `bluetooth.addCharacteristicWrittenListener(callback)` — helperと同じ完了イベントを観測する

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| クラス名・メソッド名・callback名 | `EspBleMidiHost(EspBle &)` | `EspBleMidiHost(EspBleBluedroid &)`以外は同一 |
| GATT操作の実行中に送信 | 発行できる | その場で`InvalidState`で失敗。完了イベントから送る |
| 接続できるMIDI機器 | 最大4 | Centralリンク1本 |
| ワイヤ形式 | `EspBleMidi.h`のBLE MIDI 1.0 packet | 同一ファイル・同一バイト列（`tests/unit/midi`） |

**なぜ:** どちらの違いもMIDI helperではなくGATT Client側の性質です。Central操作は直列化され、直接GATTCへの移行中はCentralリンクを1本だけ公開しています（[DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)）。`sendSysEx()`は元から同じ方法でpacketを連鎖させているため、長いSysExの扱いは変わりません。

**移植方法:** ライブラリオブジェクトの宣言を変え、連続送信はこのexampleのようにwrite完了イベントで連鎖させます。

## 期待されるSerial出力

```
MIDI: conn=1 status=0x90 data1=60 data2=100 ts=165
```
