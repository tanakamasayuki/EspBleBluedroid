# ProfileSupport

> English: [README.md](README.md)
> 概念の説明: [Bluetooth Classic通信の入門ガイド](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) Capabilityの章
> EspBle: 対応exampleなし — Bluetooth Classic専用（[DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)）

主要なBluetooth Classic profileについて、**使えるかどうかと、その理由**を表示します。Bluetooth stackは一切初期化しません。

「ここでA2DPは使えるのか」という問いの答えは1種類ではなく、こちらの取るべき対応も変わります。profileが使えない理由は、このライブラリが未実装だから、Arduino-ESP32のbuildで無効だから、ESP-IDFに公開APIが無いから、あるいはそもそも標準Classic profileが存在しないから、のいずれかです。このexampleはそのどれなのかを報告します。

## 必要なもの

- このsketchを動かす無印ESP32 × 1。peerは不要で、Bluetooth通信も一切行いません

## 動作

- profileの表（SPP、A2DP Sink / Source、AVRCP Controller / Target、HID Device / Host、HFP Hands-Free / Audio Gateway、PBAP Client、MIDI）を順に処理します
- それぞれについて**`begin()`の前に**`bluetooth.classic().profileSupport(profile)`を呼びます
- 状態名と、人が読める理由の文字列を表示します

## 5つの状態

| 状態 | 意味 | 取るべき対応 |
|---|---|---|
| `Supported` | CoreとEspBleBluedroidの両方で利用可能 | そのまま使えます |
| `LibraryNotImplemented` | Coreにはあるが、ライブラリの公開APIが未実装 | [docs/CLASSIC_PROFILE_SUPPORT.ja.md](../../../docs/CLASSIC_PROFILE_SUPPORT.ja.md)を参照するか、ESP-IDFを直接使います |
| `CoreDisabled` | Arduino-ESP32のbuild optionで無効 | Coreの再ビルドが必要で、実行時には切り替えられません |
| `CoreApiUnavailable` | ESP-IDFに利用可能な公開profile APIが無い | ライブラリ側で何をしても呼ぶ先がありません |
| `NoStandardProfile` | 標準Classic profileがそもそも存在しない | 別のprofileを使うか、SPPでデータを運びます |

## 主なAPI

- `bluetooth.classic().profileSupport(profile)` → `status`、`coreAvailable`、`implemented`、`reason`を持つ`EspBluedroidClassicProfileSupport`
- `EspBluedroidClassicProfile` — profileの列挙（`Spp`、`A2dpSink`、`A2dpSource`、`AvrcpController`、`AvrcpTarget`、`HidDevice`、`HidHost`、`HfpHandsFree`、`HfpAudioGateway`、`Hsp`、`Pan`、`PbapClient`、`PbapServer`、`Map`、`Opp`、`Ftp`、`Dun`、`Sap`、`Midi`、`Gap`）
- `bluetooth.capabilities()` — より粗い機器単位のsnapshot（`classic`、`classicInquiry`、`classicSpp`、`dualMode`、`ble`）

## メモ

- **これはcompile-timeのsnapshotです。** Coreのbuild optionとこのライブラリの実装状況を反映するもので、実行中に変化しません。だからこそ`begin()`前に呼べて、コストもかかりません。
- **GamePadはHIDの下にあります。** `HidDevice` / `HidHost`を確認してください。Arduino-ESP32 3.3.11の標準buildでは`CONFIG_BT_HID_ENABLED`が無効なため`CoreDisabled`になります。
- **Classic上のMIDIは`NoStandardProfile`です。** Bluetooth Classicに標準MIDI profileはありません。Bluetooth上のMIDIはBLEのGATT serviceであり、このライブラリでは未実装です（EspBleにはあります）。
- Classic profileが「動かない」ときは、まずこのexampleを実行してください。「buildされていない」のか「実装されていない」のかが一度で分かります。

## 期待されるSerial出力

```
SPP: supported
  SPP Server and Client APIs are available
A2DP Sink: supported
  A2DP Sink and Source SBC/PCM APIs are available; only one A2DP role may be active
AVRCP Controller: supported
  AVRCP Controller and Target passthrough and absolute-volume APIs are available
HID Device / GamePad: core-disabled
  CONFIG_BT_HID_ENABLED is disabled by the Core build
HFP Hands-Free: library-not-implemented
  HFP Hands-Free SLC, SCO, and built-in-codec PCM APIs are available; call control is still being implemented
```

（HFPが`library-not-implemented`になるのはcall controlの整備が続いているためです。
exampleが使うSLC / SCO / PCMのAPIは動作します。[HfpHandsFree](../HfpHandsFree/)を参照。）
