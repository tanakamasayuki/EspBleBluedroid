# HID・MIDI・Audio profileのAPI整備計画

## 目的

EspBleBluedroid、EspBle、EspUsbHost、EspUsbDeviceの間で、HID・MIDI・Audioを
できるだけ同じ語彙と操作感で扱えるようにする。特に次を満たすことを目標とする。

- USB Hostで受けた入力をBLE/Classic Deviceへ送り、その逆も同じ値型で書ける
- 同じVID/PIDや名前を持つ機器が複数あっても、runtime IDで取り違えない
- application callbackとbridge用listenerを併設できる
- transport固有の接続、codec、遅延、帯域制約を隠して同一仕様に見せかけない
- Bluetooth SIG、USB-IFなどが定めた標準profileだけをcore libraryで扱う

独自の「Classic MIDI over SPP」は作らない。MIDIは標準のBLE MIDIとUSB MIDIを対象とする。
ブリッジ処理そのものは別ライブラリの責務とし、このライブラリではブリッジから扱いやすい
一貫したAPI、値型、接続識別、lifecycleを提供するところまでを範囲とする。

Classic各profileの現在の対応可否、build制約、優先度は
[Bluetooth Classic profile対応表](CLASSIC_PROFILE_SUPPORT.ja.md)を正本とする。

## 現在確認できる前提

Arduino-ESP32 3.3.11の無印ESP32 buildでは、A2DP、AVRCP、HFP Client/AG、SPPは
有効である。一方、Classic HIDのESP-IDF API headerは同梱されているが
`CONFIG_BT_HID_ENABLED`は無効であり、stock buildのままではlink可能な実装として
保証できない。したがってClassic HIDは、APIを先に見せるのではなく、対応platformを
用意してcompile/link/実機試験できた時点でcapabilityを有効にする。

EspBleにはBLE HID Device/Host、BLE MIDI Device/Host、HID Report Map parser、keymap、
MIDI packet codecがある。EspUsbHost/EspUsbDeviceにはHID、USB MIDI、USB Audioの
Host/Device APIがある。これらを参考実装とし、EspBleBluedroidだけの別表現を増やさない。

## 共通APIの原則

### transportとprofileを分ける

接続や探索はtransport固有のままにする。

```text
BLE connection ID       != Classic profile session ID
Classic HID session ID  != SPP session ID
USB device address      != Bluetooth address
```

一方、接続後に流れるHID report、MIDI message、PCM formatなど、wire transportに依存しない
値は共通化する。bridgeは「接続を同一視する」のではなく、2つのendpointを明示的に対応づける。

### 複数の同一機器をidentity情報で判定しない

VID/PID、製品名、Bluetooth name、report descriptorはmetadataであり、instance identityには
使わない。Hostが公開するすべてのeventと送信APIは、ownerのlifecycle内で一意なruntime IDと
interface/function IDを持つ。切断後の古いIDは無効にし、再接続ではgenerationの異なるIDを
割り当てる。

複数接続を想定するAPIでは、暗黙の「最初の1台」へ送らない。送信先IDを必須にし、単一接続
adapterを提供する場合も、対象が一意でないときは明示的に失敗させる。

### callbackを奪い合わない

各eventはapplication向けのprimary `on*()` と、bridge/profile helper向けの
`add*Listener()` / `removeListener()`を持つ。primaryを先、listenerを登録順に配送する。
回答責任が1箇所に限られるSecurity UIや同期Readだけはprimary 1つとする。

### control pathとdata pathを分ける

接続、切断、設定変更、送信完了は`update()`から配送する。HIDとMIDIの小さいeventも
bounded queueへ値をcopyして配送する。

Audio dataは通常event queueへ積まない。固定容量ringまたはpush/pull streamを使い、
overrun、underrun、drop byte/frame、high-water markを統計として公開する。音声callbackを
stack taskから`update()`へ遅延すると締切を破る場合は、その例外をAPIコメントとガイドに
明記する。

## 共通値型の方針

利用側や外部bridge libraryが型変換を繰り返さずに済むよう、次を小さなbackend非依存header群として
切り出す。配置とpackage名は、4ライブラリを同時に更新できる最初の変更で確定する。

### HID

- 6KRO/NKRO keyboard state、modifier、LED output state
- mouse、consumer control、system control、gamepadの正規化event
- raw report、report ID/type、usage page/usage、field value
- HID Report Map parserとkeyboard layout/keymap
- ownerごとに一意なHID device ID、interface/collection ID

EspBleのHID値型を基準にし、EspUsbHost/EspUsbDeviceの既存型で不足するidentity fieldを
洗い出す。Boot/Report protocolやtransport固有metadataは捨てず、共通部分の外側に保持する。

### MIDI

- status、data1、data2、SysEx payload、cable/group、timestamp
- noteOn/noteOff/controlChange/programChange等の送信helper
- running statusとSysExのparser/encoder

BLEの13-bit timestampとUSB MIDI 1.0 event packetはtransport codecで相互変換し、共通message
には時刻の有無とclock domainを明示する。Classicには標準MIDI profileがないため、
`classic().midi()`は追加しない。

### Audio

- PCM format: sample rate、channel数、container bytes、valid bits、interleaved、endianness
- encoded format: SBC、CVSD、mSBCなどのcodec種別とcodec configuration
- stream方向、stream state、mute/volume、buffer統計、timestamp/frame sequence

「Playback/Capture」がHost/Deviceで逆に読める問題を避けるため、共通data pathでは
ローカルESP視点のReceive/Transmitを正本とし、USBやA2DPのprofile名との対応表を用意する。

## 実装フェーズ

### Phase 0: cross-library contractとfeasibility test

1. 4ライブラリのHID/MIDI/Audio型とmethodを対応表にする。
2. 共通値型headerの候補とownership/lifetime規則をunit testで固定する。
3. stock Arduino-ESP32とHID-enabled platformのcompile/link matrixを作る。
4. A2DP/AVRCP/HFP/HIDのcapabilityをcompile-time設定と実link可否から生成する。
5. profileごとのcontroller/host resource競合表を作る。

完了条件は、未実装profileが誤って利用可能と報告されず、同一deviceを複数並べたtest fixtureを
runtime IDで区別できる契約が決まっていること。

### Phase 1: EspBleBluedroidのBLE HID/MIDI parity

汎用GATT Serverが整ったため、まずEspBleの標準profile helperをBluedroidへ移す。

1. HID Report Map parser、keymap、report値型を共通化する。
2. BLE HID Device（keyboard、mouse、consumer/system control、gamepad、vendor/custom）を追加する。
3. BLE HID Hostを追加し、connection IDごとに複数の同一機器を区別する。
4. BLE MIDI Device/Hostを追加し、USB MIDIと同じmessage/send helperへ揃える。
5. primary callbackとlistenerをgeneric GATT/connection eventにも追加する。

EspBleの同名exampleと同じ章・scenarioを使い、Bluedroid差だけ派生側に記述する。

### Phase 2: Classic profile session基盤

SPP固有実装をそのまま他profileへ複製せず、Classic共通の次の仕組みを先に切り出す。

- address、runtime session ID、incoming/outgoing、Security状態を持つsnapshot
- 非同期connect/disconnect/failure、primary callback＋listener
- profileごとの複数session registryと、stale IDを拒否するgeneration管理
- bounded control-event queueとdata-path別統計
- `end()`、認証失敗、同時接続、profile間resource競合のcleanup規則

SPPの現在の単一session制限は、この基盤を実機で固定してから拡張する。

### Phase 3: Classic HID Host/Device

公開入口はBLE HIDと対応させる。

```text
bluetooth.hidHost()                    // BLE HOGP
bluetooth.classic().hidHost()          // Classic HID Host
bluetooth.hidKeyboard()                // BLE HID Device
bluetooth.classic().hidKeyboard()      // Classic HID Device
```

値型、`onKeyboard()`、`onMouse()`、`onGamepad()`、`sendReport()`、LED output stateなどは
共通にし、接続IDだけtransport固有型にする。Classic固有のprotocol mode、virtual cable、
reconnect initiate、control/interrupt channelはClassic snapshot/configへ残す。

stock Arduino-ESP32 3.3.11ではClassic HIDが無効なため、次のどちらかが成立するまで実装済みと
しない。

- official platformが`CONFIG_BT_HID_ENABLED`を有効にする
- libraryが明示的にサポートするHID-enabled platform packageを用意し、CIと実機試験を持つ

headerだけ存在する状態や、常に`Unsupported`を返す見せかけのAPIは公開しない。

### Phase 4: A2DP Source/SinkとAVRCP

`classic().a2dpSink()` / `classic().a2dpSource()`を別objectとして公開し、session ID付きの
接続・stream state・codec configを扱う。AVRCP Controller/Targetはaudio data pathへ混ぜず、
再生操作、absolute volume、metadata eventのcontrol planeとして分離する。

Arduino-ESP32 3.3.11標準buildでは`CONFIG_BT_A2DP_USE_EXTERNAL_CODEC`が無効であり、
新しいencoded audio buffer APIは実動しない。実機検証の結果に基づき、root libraryの正本は
Core内蔵SBC codecがdecode/encodeする16-bit interleaved PCMとする。resampleとchannel変換は
別libraryへ分離し、USB Audio bridgeは次のpipelineとして構成する。

```text
A2DP Sink (Core SBC decode) -> PCM queue/resample -> USB Audio Device Capture/Playback
USB Audio Host -> PCM queue/resample -> A2DP Source (Core SBC encode)
```

SBC処理はCore設定により不可避だが、queue、resample、channel変換を暗黙に行わず、追加heap、
latency、dropを各stageで観測可能にする。external-codec-enabled buildは標準support対象に
含めず、別の実機fixtureが成立した場合に再評価する。

### Phase 5: HFP Hands-Free / Audio Gateway

`classic().handsFree()`と`classic().audioGateway()`を分け、call controlと同期audioを別data pathに
する。CVSD/mSBC、8/16 kHz、mono、bad-frame、preferred frame sizeを明示する。A2DPと同じ
PCM streamに見せるadapterは作れるが、帯域・frame deadline・call stateを消さない。

### Phase 6: 外部ライブラリとのAPI適合確認

このphaseでもbridge機能は実装しない。公開APIだけで独立bridge libraryを無理なく構築できるかを
compile testと小さな検証applicationで確認する。endpointの対応づけ、再接続policy、loop防止、
format変換、実際のdata転送は独立bridge libraryで扱う。

- USB HID Host -> BLE/Classic HID Device、および逆方向
- BLE MIDI Host/Device <-> USB MIDI Host/Device
- A2DP/HFP <-> USB Audio（明示codec/PCM pipeline経由）

複数の同一deviceについて、1対1固定mapping、選択UI、broadcastのいずれかをapplicationが選ぶ。
勝手に「最初に接続した機器」を選ばない。

## テスト計画

各phaseは次の順で進める。

1. backend非依存codec/value unit test。可能な限り4ライブラリで同じfixtureを使う。
2. profile単体の2台peer test。Host/Device、両方向data、切断、再接続、Securityを確認する。
3. 同一VID/PID・同一name・同一report mapを持つ複数deviceの識別test。
4. BLE/Classic/USBを同時に参照するAPI適合test。外部実装が接続順に依存せず明示的にmappingできることを確認する。
5. Audioは短時間のbit-exact fixtureに加え、長時間soakでoverrun/underrun、latency、heapを測る。
6. `end()`/再初期化、認証失敗、peer消失、queue飽和で古いIDやcallbackが残らないことを確認する。

2台では複数同一Bluetooth deviceを検証できないため、そのphaseでは3台目または再現可能な
external fixtureを必須とする。USB bridge試験はUSB Host/Device対応SoCを別fixtureとして使い、
無印ESP32だけで成立した扱いにしない。

## 今回の非対象

- 独自Classic MIDI protocol
- 標準互換性が確認できないvendor codec/profile
- 送信先を暗黙選択する複数device bridge
- root library内での自動transcode/resample
- capabilityがfalseのprofileを形だけ公開すること

## 着手順の結論

次の実装着手はPhase 0、その次はBLE HID/MIDI parityとする。Classic HIDはplatform buildの
制約を解決してから、AudioはA2DP/AVRCPをHFPより先に進める。これにより、先に共通値型と
複数device契約を固定し、後からClassic/USB bridgeを足してもAPIを作り直さずに済む。
