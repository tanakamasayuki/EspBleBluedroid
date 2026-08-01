# Bluetooth Classic profile対応表

## 判定条件

この表は、無印ESP32とArduino-ESP32 3.3.11の標準buildを対象にしたsnapshotです。
ESP32-S3/C3/C6など、Bluetooth Classic controllerを持たないSoCは対象外です。

対応可否は、次の3段階をすべて満たすかで判定します。

1. Bluetooth SIGで標準化されたClassic profileである
2. Arduino-ESP32が使用するESP-IDF/Bluedroidにpublic APIとlink可能な実装がある
3. EspBleBluedroidが公開API、example、実機testを提供している

ESP-IDFのheaderが存在するだけの場合や、build optionで無効な機能は「対応」と数えません。

## 優先度

| 優先度 | 意味 |
|---|---|
| P0 | 必須。すでに利用可能で、互換性と回帰testを維持する |
| P1 | 最優先で追加する |
| P2 | P1の共通基盤後に追加する |
| P3 | platform対応と実需要を確認してから検討する |
| 対象外 | 標準profileがない、廃止傾向、または利用可能なpublic backendがない |

このライブラリの必須範囲は、SPPに加えてサウンド系のA2DP Sink/Source、AVRCP
Controller/Target、HFP Hands-Free/Audio Gatewayです。P2は任意という意味ではなく、
A2DP/AVRCPのdata pathを確立してからHFPへ進む実装順を表します。GamePadを含むHIDも
P1ですが、現在はplatform buildに阻まれているため保留扱いです。

## 主要profile一覧

| Profile | 主な用途・role | Arduino-ESP32 3.3.11 | EspBleBluedroid | 優先度 | 判定・制約 |
|---|---|---|---|---|---|
| GAP | Inquiry、接続性、device name、Security | 利用可能 | 対応済み | P0 | `classic().inquiry()`、Security、bond管理を公開。profile共通基盤として維持する |
| SPP | RFCOMM上のbinary serial、Server/Client | `CONFIG_BT_SPP_ENABLED=y` | 対応済み | P0 | Server/Client、session、binary I/O、`Stream` adapter、Security、実機testあり。絶対に維持する |
| A2DP Sink | 音楽受信、SBCをCoreで復号したPCM | `CONFIG_BT_A2DP_ENABLE=y` | 対応済み | P1 | `classic().a2dpSink()`。16-bit interleaved PCM、codec設定、session、stream状態を公開。PCM callbackはstack task上で同期実行する |
| A2DP Source | PCMをCoreでSBC encodeして音楽送信 | `CONFIG_BT_A2DP_ENABLE=y` | 対応済み | P1 | `classic().a2dpSource()`。PCM要求callback、session、stream制御を公開。Sink/Sourceの同時利用と複数A2DP接続はCore制約により非対応 |
| AVRCP Controller | 再生・停止・選曲・音量操作 | `CONFIG_BT_AVRCP_ENABLED=y` | 未対応 | P1 | A2DPと同時に整備する。audio dataではなくcontrol planeとして分離する |
| AVRCP Target | 再生状態、metadata、absolute volume | `CONFIG_BT_AVRCP_ENABLED=y` | 未対応 | P1 | A2DPと同時に整備する。Controller/Targetのroleを混同しない |
| HID Device | Keyboard、Mouse、GamePad等として動作 | **`CONFIG_BT_HID_ENABLED`無効** | **非対応** | P1（保留） | GamePadは独立profileではなくHID report descriptorで表現できる。ただし現状の標準buildではlink可能な実装を保証できない |
| HID Host | Keyboard、Mouse、GamePad等を受信 | **`CONFIG_BT_HID_ENABLED`無効** | **非対応** | P1（保留） | `esp_hidh_api.h`は存在するがheaderだけでは対応扱いにしない。HID-enabled platformと実機testが必要 |
| HFP Hands-Free | Headset側、通話制御、CVSD/mSBC音声 | `CONFIG_BT_HFP_CLIENT_ENABLE=y` | 未対応 | P2 | サウンド系。A2DPの後にcall controlと同期audio data pathを分離して追加する |
| HFP Audio Gateway | Phone側、通話制御、CVSD/mSBC音声 | `CONFIG_BT_HFP_AG_ENABLE=y` | 未対応 | P2 | Hands-Freeとrole別objectにする。HCI audio data pathとWBSが現在のbuildで有効 |
| HSP | 旧式Headset profile | 専用public APIなし | 非対応 | 対象外 | HFPを優先する。互換性要求とbackend保証が確認できるまで追加しない |
| PAN | IP network、PANU/NAP/GN | public profile APIなし | 非対応 | P3 | BNEP/PANを安定して公開できるbackendを確保できた場合だけ再評価する |
| PBAP Client | 電話帳受信、PCE | **`CONFIG_BT_PBAC_ENABLED`無効** | 非対応 | P3 | `esp_pbac_api.h`は存在するが、現状buildでは無効。HIDと同様にheaderだけでは対応扱いにしない |
| PBAP Server | 電話帳提供、PSE | public profile APIなし | 非対応 | P3 | backendと実需要を確認してから検討する |
| MAP | Message Access、通知・message操作 | public profile APIなし | 非対応 | P3 | backendと実需要を確認してから検討する |
| OPP / FTP | object/file転送 | public profile APIなし | 非対応 | 対象外 | 新規core APIの対象にしない。必要なら標準backendを確保して再評価する |
| DUN | modem接続 | public profile APIなし | 非対応 | 対象外 | 用途が古く、SPPやnetwork stackで代替できるため追加しない |
| SAP | SIM Access | public profile APIなし | 非対応 | 対象外 | platform非対応のため追加しない |
| MIDI | MIDI event転送 | **標準Classic profileなし** | 非対応 | 対象外 | 独自SPP protocolは作らない。標準のBLE MIDIとUSB MIDIだけを対象にする |

「public profile APIなし」は、現在同梱されているESP-IDF header群に、そのprofileを
EspBleBluedroidから安定して公開するためのprofile APIが見つからないことを意味します。
内部Bluedroid componentの有無だけでは対応可能とは判定しません。

## GamePadの扱い

Classic GamePadはHID Device、GamePadを接続して読む側はHID Hostです。GamePad専用の
Bluetooth profileを新設する必要はなく、標準HIDのReport protocolとreport descriptorを使います。
このため、HID backendが有効になればKeyboard、Mouse、GamePad、Consumer Controlなどを
同じHID基盤へ載せられます。

ただし現在のArduino-ESP32 3.3.11標準buildでは`CONFIG_BT_HID_ENABLED`が無効です。
`esp_hidd_api.h`と`esp_hidh_api.h`は同梱されていますが、次のいずれかを満たすまでは
EspBleBluedroidのClassic GamePadを「非対応」と表示します。

- Arduino-ESP32の公式buildでClassic HIDが有効になる
- 本ライブラリがサポートするHID-enabled platform packageを用意し、compile/link/実機testを持つ

常に`Unsupported`を返すAPIや、利用不能なheaderだけを先行公開することはしません。

## 実装順

1. **P0維持:** SPPとClassic共通のInquiry、Security、bond、session lifecycle
2. **P1 Sound:** A2DP Sink/SourceとAVRCP Controller/Target
3. **P1 HID:** HID-enabled buildの確保後、HID Device/HostとGamePad example
4. **P2 Call Audio:** HFP Hands-Free/Audio Gateway
5. **P3再評価:** PAN、PBAP、MAPはbackendと利用例を確認して個別判断

HIDのplatform制約解消を待つ間も、A2DP/AVRCPの作業は独立して進められます。P1内の
実際の着手順は、利用可能なbackendを優先してA2DP/AVRCP、次にHIDとします。

## A2DPのCore制約

Arduino-ESP32 3.3.11標準buildは`CONFIG_BT_A2DP_USE_EXTERNAL_CODEC`が無効です。
新しいencoded audio buffer APIはheaderとlink可能なsymbolを持ちますが、この構成では
Source送信が`ESP_FAIL`となり、Sinkへencoded callbackも配送されないことを実機で確認しました。
このためEspBleBluedroidは、Core内蔵SBC codecにつながるlegacy PCM callbackを公開backendに
使用します。

- Sink: SBCをCoreが復号し、16-bit interleaved PCMを`onPcmData()`へ渡す
- Source: `onPcmRequested()`で16-bit interleaved PCMを受け取り、CoreがSBCへencodeする
- PCM callbackはA2DP stack task上の同期処理。pointerはcallback中だけ有効で、blockしない
- 接続、切断、stream状態などcontrol eventは`bluetooth.update()`から配送する
- codec設定通知より早いPCM bufferは形式不明のため公開しない
- Coreの上限に合わせ、A2DP SinkまたはSourceのどちらか1 role、1 sessionだけを許可する

この制約はUSB Audio等とのbridgeを本ライブラリへ実装するものではありません。別libraryは
PCM formatを見てqueue、resample、channel変換を明示的に構成します。

## 完了の定義

各profileは次を満たして初めて「対応済み」に変更します。

- 対応roleを区別した公開APIがある
- 複数sessionを取り違えないruntime IDとlifecycleがある
- `end()`、切断、失敗、再接続を含むexampleがある
- binary/audio/report dataを実機で双方向確認している
- build optionとSoC制約を`capabilities()`および文書から確認できる
