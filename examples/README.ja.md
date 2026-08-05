# EspBleBluedroid Examples

> English: [README.md](README.md)
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](DIFFERENCES_FROM_ESPBLE.ja.md)

**無印ESP32**（Bluetooth Classicを持つESP32 SoC）向けの101個のexampleです。
Arduino-ESP32同梱のBluedroid backendを使います。

BLE側のexampleは兄弟ライブラリ
[EspBle](https://github.com/tanakamasayuki/EspBle)から移植したもので、APIも同じです。
そのため、sketchは通常は名前の置き換えだけで移ります。使い方が実際に異なる箇所は、
そのexampleのREADMEに**「EspBleとの違い」**として理由と移植手順つきで書いています。
ライブラリ全体に共通する一覧は
[DIFFERENCES_FROM_ESPBLE.ja.md](DIFFERENCES_FROM_ESPBLE.ja.md)にあります。

## 概念はガイドにまとまっています

| 知りたいこと | ガイド | 対応するexample |
|---|---|---|
| BLE: Advertising・Scan・接続・アドレス | [BLE入門ガイド](../docs/GUIDE_BLE_BASICS.ja.md) GAP編 | [Gap/](Gap/) |
| BLE: ペアリング・ボンディング・認証方式 | 同 セキュリティ編 | [Security/](Security/) |
| BLE: Service・Characteristic・Read/Write・Notify | 同 GATT編 | [Gatt/](Gatt/) |
| Classic: Inquiry・SPP・profile・pairing | [Classic入門ガイド](../docs/GUIDE_CLASSIC_BASICS.ja.md) | [Classic/](Classic/) |
| 両方を同時に動かす | Classic入門ガイド dual mode | [DualMode/](DualMode/) |

各exampleのREADMEはそれ単体で読めるように書いてあるので、ガイドを読まずに個別の
exampleから入っても構いません。

## ビルド

各exampleには検証済みのArduino-ESP32バージョンを固定した`sketch.yaml`が付いているので、
IDEでのボード設定は不要です。

```sh
arduino-cli compile --profile esp32 examples/<path>
```

profileは`esp32`の1つだけです。このライブラリは無印ESP32専用で、ESP32ファミリで
Bluetooth Classicを持つのは無印ESP32だけだからです。

## 一覧

### はじめに

| Example | 役割 | 説明 |
|---|---|---|
| [CompileSmoke](CompileSmoke/) | — | 最小のビルド確認。共通APIを一通り触り、ライブラリのバージョンを表示 |
| [Classic/ProfileSupport](Classic/ProfileSupport/) | — | どのClassic profileが使えるか、その理由。stackは開始しない |

### GAP — Advertising・Scan・接続

| Example | 役割 | 説明 |
|---|---|---|
| [Gap/Advertise](Gap/Advertise/) | Peripheral | name、Service UUID、Manufacturer Data、channel map付きのconnectable Legacy Advertising |
| [Gap/Scan](Gap/Scan/) | Central | address / RSSI / nameを表示し続けるActive Scan |
| [Gap/Connect](Gap/Connect/) | Central | Service UUIDで探して接続。非同期の接続・切断・失敗イベント |
| [Gap/Mtu](Gap/Mtu/) | Central | 希望MTUの交換とNotification payload上限 |
| [Gap/ConnectionParameters](Gap/ConnectionParameters/) | Central | 確立済みlinkのinterval / latency / timeout変更 |
| [Gap/Beacon](Gap/Beacon/) | Broadcaster | 非connectable・非scannableなbeacon。Manufacturer Dataとinterval制御 |
| [Gap/IBeacon](Gap/IBeacon/) | Broadcaster | Apple iBeaconの送信（UUID / major / minor / measured power） |
| [Gap/ServiceData](Gap/ServiceData/) | Broadcaster | 温度をService Data（AD 0x16）で接続なしに公開 |
| [Gap/ScanResponse](Gap/ScanResponse/) | Peripheral | payloadをAdvertisingとScan Responseに分けて31 byte制限を越える |
| [Gap/AcceptList](Gap/AcceptList/) | Peripheral | Filter Accept Listで接続元を制限し、Scanも絞り込む |
| [Gap/DirectedAdvertising](Gap/DirectedAdvertising/) | Peripheral | 特定の1台へ向けたDirected Advertising。payloadは載らない |
| [Gap/PrivateAddress](Gap/PrivateAddress/) | Peripheral | Random StaticまたはRPAでのAdvertising |

### GATT — Basics（基本機構とシリアル）

| Example | 役割 | 説明 |
|---|---|---|
| [Gatt/Basics/Server](Gatt/Basics/Server/) | Peripheral | 独自Service、Read/Write可能なCharacteristic、Descriptor、読まれた瞬間に作る値 |
| [Gatt/Basics/Client](Gatt/Basics/Client/) | Central | Database Discovery → Read → Write → Descriptorの流れ |
| [Gatt/Basics/NotifyServer](Gatt/Basics/NotifyServer/) | Peripheral | 購読されている間だけ定期Notification |
| [Gatt/Basics/SubscribeClient](Gatt/Basics/SubscribeClient/) | Central | NotifyServerを購読してNotificationを表示 |
| [Gatt/Basics/AutoReconnectClient](Gatt/Basics/AutoReconnectClient/) | Central | linkと購読を手で維持する |
| [Gatt/Basics/IndicateServer](Gatt/Basics/IndicateServer/) | Peripheral | 応答ありIndicationと`onSent()`での配送確認 |
| [Gatt/Basics/IndicateClient](Gatt/Basics/IndicateClient/) | Central | IndicateServerのIndicationを購読 |
| [Gatt/Basics/NusServer](Gatt/Basics/NusServer/) | Peripheral | NUS互換のRX WriteとTX Notification echo |
| [Gatt/Basics/NusClient](Gatt/Basics/NusClient/) | Central | NUS互換のTX購読とRX Write Without Response |

### GATT — Device・時刻・管理

| Example | 役割 | 説明 |
|---|---|---|
| [Gatt/Device/BatteryServer](Gatt/Device/BatteryServer/) | Peripheral | 標準Battery LevelのReadとNotification |
| [Gatt/Device/BatteryClient](Gatt/Device/BatteryClient/) | Central | Battery LevelのReadと購読 |
| [Gatt/Device/DeviceInfoServer](Gatt/Device/DeviceInfoServer/) | Peripheral | 標準Device Informationの文字列とPnP ID |
| [Gatt/Device/DeviceInfoClient](Gatt/Device/DeviceInfoClient/) | Central | Device Informationの順次ReadとPnP IDのデコード |
| [Gatt/Device/UserDataServer](Gatt/Device/UserDataServer/) | Peripheral | AgeとFirst NameのRead/Write、Database Change IncrementのNotify |
| [Gatt/Device/UserDataClient](Gatt/Device/UserDataClient/) | Central | Age/First Nameの書き込みとDatabase Change Incrementの観測 |
| [Gatt/Device/BondManagementServer](Gatt/Device/BondManagementServer/) | Peripheral | Bond Management FeatureとControl Pointのbond削除op code |
| [Gatt/Device/BondManagementClient](Gatt/Device/BondManagementClient/) | Central | Feature bit fieldのReadとbond削除op codeのWrite |
| [Gatt/Time/CurrentTimeServer](Gatt/Time/CurrentTimeServer/) | Peripheral | 標準10 byte Current TimeのReadとNotification |
| [Gatt/Time/CurrentTimeClient](Gatt/Time/CurrentTimeClient/) | Central | Current Timeのデコードと購読 |
| [Gatt/Time/ReferenceTimeUpdateServer](Gatt/Time/ReferenceTimeUpdateServer/) | Peripheral | Time Update Control PointでTime Update Stateを駆動 |
| [Gatt/Time/ReferenceTimeUpdateClient](Gatt/Time/ReferenceTimeUpdateClient/) | Central | 参照更新の要求・取消と状態のRead |

### GATT — センサー

| Example | 役割 | 説明 |
|---|---|---|
| [Gatt/Sensors/EnvironmentalServer](Gatt/Sensors/EnvironmentalServer/) | Peripheral | 標準の温度・湿度・気圧 |
| [Gatt/Sensors/EnvironmentalClient](Gatt/Sensors/EnvironmentalClient/) | Central | スケール付きセンサー値のReadと温度Notificationの購読 |

### GATT — ヘルスケア

| Example | 役割 | 説明 |
|---|---|---|
| [Gatt/Health/HeartRateServer](Gatt/Health/HeartRateServer/) | Peripheral | 標準Heart Rate MeasurementとBody Sensor Location |
| [Gatt/Health/HeartRateClient](Gatt/Health/HeartRateClient/) | Central | flags駆動のHeart Rate Measurementデコードと購読 |
| [Gatt/Health/HealthThermometerServer](Gatt/Health/HealthThermometerServer/) | Peripheral | IEEE-11073 FLOATのTemperature Measurement IndicationとTemperature Type |
| [Gatt/Health/HealthThermometerClient](Gatt/Health/HealthThermometerClient/) | Central | Temperature TypeのReadとFLOAT Indicationのデコード |
| [Gatt/Health/BloodPressureServer](Gatt/Health/BloodPressureServer/) | Peripheral | IEEE-11073 SFLOATの収縮期/拡張期/平均IndicationとFeature |
| [Gatt/Health/BloodPressureClient](Gatt/Health/BloodPressureClient/) | Central | FeatureのReadとSFLOAT Indicationのデコード |
| [Gatt/Health/WeightScaleServer](Gatt/Health/WeightScaleServer/) | Peripheral | uint16 Weight Measurement Indication（0.005 kg）とFeature |
| [Gatt/Health/WeightScaleClient](Gatt/Health/WeightScaleClient/) | Central | FeatureのReadとWeight Measurementのデコード |
| [Gatt/Health/BodyCompositionServer](Gatt/Health/BodyCompositionServer/) | Peripheral | 体脂肪率と任意のWeight IndicationとFeature |
| [Gatt/Health/BodyCompositionClient](Gatt/Health/BodyCompositionClient/) | Central | FeatureのReadと体脂肪率/体重のデコード |
| [Gatt/Health/PulseOximeterServer](Gatt/Health/PulseOximeterServer/) | Peripheral | SFLOATのSpO2/脈拍Spot-Check IndicationとFeatures |
| [Gatt/Health/PulseOximeterClient](Gatt/Health/PulseOximeterClient/) | Central | FeaturesのReadとSpO2/脈拍のデコード |
| [Gatt/Health/GlucoseServer](Gatt/Health/GlucoseServer/) | Peripheral | Record Access Control Point: RACP Write → Measurement Notify → RACP Indicate |
| [Gatt/Health/GlucoseClient](Gatt/Health/GlucoseClient/) | Central | RACPのreport-records要求と測定値/応答のデコード |
| [Gatt/Health/ContinuousGlucoseMonitoringServer](Gatt/Health/ContinuousGlucoseMonitoringServer/) | Peripheral | E2E-CRC付きCGM FeatureとMeasurement Notification |
| [Gatt/Health/ContinuousGlucoseMonitoringClient](Gatt/Health/ContinuousGlucoseMonitoringClient/) | Central | E2E-CRCの検証とSFLOAT血糖値/time offsetのデコード |

### GATT — フィットネス

| Example | 役割 | 説明 |
|---|---|---|
| [Gatt/Fitness/CyclingSpeedCadenceServer](Gatt/Fitness/CyclingSpeedCadenceServer/) | Peripheral | wheel/crank複合のCSC Notification、Feature、Sensor Location |
| [Gatt/Fitness/CyclingSpeedCadenceClient](Gatt/Fitness/CyclingSpeedCadenceClient/) | Central | Sensor LocationのReadとCSC Measurementのデコード |
| [Gatt/Fitness/RunningSpeedCadenceServer](Gatt/Fitness/RunningSpeedCadenceServer/) | Peripheral | 速度/ケイデンス/ストライド/距離のRSC Notification、Feature、Sensor Location |
| [Gatt/Fitness/RunningSpeedCadenceClient](Gatt/Fitness/RunningSpeedCadenceClient/) | Central | Sensor LocationのReadとRSC Measurementのデコード |
| [Gatt/Fitness/CyclingPowerServer](Gatt/Fitness/CyclingPowerServer/) | Peripheral | 符号付き16 bitパワーのCycling Power Notification、Feature、Sensor Location |
| [Gatt/Fitness/CyclingPowerClient](Gatt/Fitness/CyclingPowerClient/) | Central | Sensor LocationのReadと符号付きパワーのデコード |
| [Gatt/Fitness/FitnessMachineServer](Gatt/Fitness/FitnessMachineServer/) | Peripheral | Fitness Machine（FTMS）のIndoor Bike Data NotificationとFeature |
| [Gatt/Fitness/FitnessMachineClient](Gatt/Fitness/FitnessMachineClient/) | Central | FeatureのReadとflags駆動のIndoor Bike Dataデコード |
| [Gatt/Fitness/LocationNavigationServer](Gatt/Fitness/LocationNavigationServer/) | Peripheral | Location and Speed Notification（速度 + sint32緯度経度）とLN Feature |
| [Gatt/Fitness/LocationNavigationClient](Gatt/Fitness/LocationNavigationClient/) | Central | LN FeatureのReadとLocation and Speedのデコード |

### GATT — アラート・近接

| Example | 役割 | 説明 |
|---|---|---|
| [Gatt/Alerts/AlertNotificationServer](Gatt/Alerts/AlertNotificationServer/) | Peripheral | カテゴリbitmaskのRead、Control PointのWrite、New Alert Notification |
| [Gatt/Alerts/AlertNotificationClient](Gatt/Alerts/AlertNotificationClient/) | Central | Control Pointの「Notify New Alert Immediately」とNew Alertのデコード |
| [Gatt/Alerts/ImmediateAlertServer](Gatt/Alerts/ImmediateAlertServer/) | Peripheral | Find Meのtarget側。Alert LevelのWrite Without Response処理 |
| [Gatt/Alerts/ImmediateAlertClient](Gatt/Alerts/ImmediateAlertClient/) | Central | Find Meのlocator側。Alert Levelの発報と解除 |
| [Gatt/Alerts/PhoneAlertStatusServer](Gatt/Alerts/PhoneAlertStatusServer/) | Peripheral | Alert Status / Ringer SettingのNotifyとRinger Control Pointのsilent mode |
| [Gatt/Alerts/PhoneAlertStatusClient](Gatt/Alerts/PhoneAlertStatusClient/) | Central | Alert StatusのRead、Ringer Control Point操作、Ringer Settingのデコード |
| [Gatt/Alerts/ProximityServer](Gatt/Alerts/ProximityServer/) | Peripheral | Proximity Reporter: Link Loss Alert LevelとTx Power（2 Service） |
| [Gatt/Alerts/ProximityClient](Gatt/Alerts/ProximityClient/) | Central | Proximity Monitor: Tx PowerのReadとLink Loss Alert Levelの設定 |

### Security

| Example | 役割 | 説明 |
|---|---|---|
| [Security/](Security/) | — | どちらの側で動かすか、EspBleとServer構成が非対称な理由 |
| [Security/JustWorksServer](Security/JustWorksServer/) | Peripheral | Just Works Pairing + Bondingと暗号化Characteristic |
| [Security/JustWorksClient](Security/JustWorksClient/) | Central | Just Works Pairing、bond store、暗号化Read |
| [Security/StaticPasskeyServer](Security/StaticPasskeyServer/) | Peripheral | 静的passkeyによるMITM認証（表示側） |
| [Security/StaticPasskeyClient](Security/StaticPasskeyClient/) | Central | passkey入力側。`requestSecurity()`と認証必須Read |
| [Security/RuntimePasskeyClient](Security/RuntimePasskeyClient/) | Central | `providePasskey()`で実行時にpasskeyを渡す |
| [Security/NumericComparisonClient](Security/NumericComparisonClient/) | Central | 両側に表示された6桁を確認する |

### HID over GATT

| Example | 役割 | 説明 |
|---|---|---|
| [Hid/KeyboardDevice](Hid/KeyboardDevice/) | Peripheral | BLE HID keyboard。Report送信、HostのLED Output Report、Protocol Mode、battery |
| [Hid/KeyboardNkro](Hid/KeyboardNkro/) | Peripheral | N-key rollover。キーボード全体の状態を29 byteの1 Reportで送る |
| [Hid/Mouse](Hid/Mouse/) | Peripheral | ボタンとホイールを備えた相対ポインタ。ドラッグは「押したままの移動」 |
| [Hid/ConsumerControl](Hid/ConsumerControl/) | Peripheral | メディアキー。1 ReportにConsumer pageのusageを1つ（16 bit） |
| [Hid/CompositeKeyboardMouse](Hid/CompositeKeyboardMouse/) | Peripheral | keyboardとmouseを1つのHID Serviceに載せ、Report IDで区別 |
| [Hid/VendorDevice](Hid/VendorDevice/) | Peripheral | 任意サイズのvendor定義Input・Output・Feature Report |
| [Hid/CustomDevice](Hid/CustomDevice/) | Peripheral | 任意のReport Descriptorと利用者が宣言するReport |
| [Hid/KeyboardHost](Hid/KeyboardHost/) | Central | HID Host。0x1812をscanしてPairing、ReportをDiscoveryし、キーをdecodeしてLEDを書き込む |

### BLE MIDI

| Example | 役割 | 説明 |
|---|---|---|
| [Midi/MidiDevice](Midi/MidiDevice/) | Peripheral | BLE MIDI楽器。ノート、Control Change、SysEx、Hostから届くMIDI |
| [Midi/MidiHost](Midi/MidiHost/) | Central | BLE MIDI Host。Discovery・購読・受信MIDIのデコード・ノート送信 |

### Bluetooth Classic — このライブラリのみ

| Example | 役割 | 説明 |
|---|---|---|
| [Classic/Inquiry](Classic/Inquiry/) | — | Classic機器の探索。address、name、RSSI、Class of Device |
| [Classic/SppServer](Classic/SppServer/) | SPP server | 認証なしSPP serverでpacketをecho |
| [Classic/SppClient](Classic/SppClient/) | SPP client | addressでSPP serverへ接続してデータ交換 |
| [Classic/SppSerialServer](Classic/SppSerialServer/) | SPP server | SPPをArduino `Stream`として`Serial`と橋渡し |
| [Classic/SppSerialClient](Classic/SppSerialClient/) | SPP client | 同じ`Stream`ブリッジの発信側 |
| [Classic/SppSecurity](Classic/SppSecurity/) | SPP server | SSP Numeric Comparisonによる認証・暗号化SPP |
| [Classic/SppPasskey](Classic/SppPasskey/) | SPP server | SSP Passkey Entryによる認証・暗号化SPP |
| [Classic/A2dpSink](Classic/A2dpSink/) | A2DP sink | 音楽をPCMで受信。AVRCP Controllerも動作 |
| [Classic/A2dpSource](Classic/A2dpSource/) | A2DP source | スピーカーへPCMを送信。AVRCP Targetも動作 |
| [Classic/HfpHandsFree](Classic/HfpHandsFree/) | HFP HF | ヘッドセットrole。SLC、SCO、双方向mono PCM |
| [Classic/HfpAudioGateway](Classic/HfpAudioGateway/) | HFP AG | スマートフォンrole。ヘッドセットを受け入れて通話音声を運ぶ |
| [Classic/ProfileSupport](Classic/ProfileSupport/) | — | profileごとの対応状況とその理由 |

### Dual mode

| Example | 役割 | 説明 |
|---|---|---|
| [DualMode/ScanWhileSpp](DualMode/ScanWhileSpp/) | 両方 | Classic SPP session接続中のBLE Scan |

### 診断

| Example | 役割 | 説明 |
|---|---|---|
| [Info/ScanDump](Info/ScanDump/) | 診断 | Advertisementの全フィールドをダンプ（UUID、Manufacturer Dataなど） |
| [Info/ConnectionInspector](Info/ConnectionInspector/) | 診断 | 対話的に接続し、MTU・security状態・bond・カウンタをダンプ |

## 2台での組み合わせ

- Gap/Advertise ↔ Gap/Scan
- Gatt/Basics/Server ↔ Gatt/Basics/Client
- Gatt/Basics/NotifyServer ↔ Gatt/Basics/SubscribeClient / AutoReconnectClient（およびGap/Mtu）
- Gatt/Basics/IndicateServer ↔ Gatt/Basics/IndicateClient
- Gatt/Basics/NusServer ↔ Gatt/Basics/NusClient
- 各`Gatt/<Category>/<Name>Server` ↔ 対応する`…Client`（Device、Time、Sensors、Health、Fitness、Alerts）
- Security/JustWorksServer ↔ Security/JustWorksClient
- Security/StaticPasskeyServer ↔ Security/StaticPasskeyClient
- Security/RuntimePasskeyClientとNumericComparisonClient ↔ スマートフォン、EspBleのボード、raw ESP-IDFのpeer（[Security/README.ja.md](Security/README.ja.md)）
- `Hid/*`の各デバイス ↔ PC・スマホ・タブレット（OSのBluetooth設定からPairing）。Hid/VendorDeviceとHid/CustomDeviceはReportを書き込めるHostが必要
- Midi/MidiDevice ↔ Midi/MidiHost（スマホ/タブレットのDAWや市販BLE MIDI楽器でも可）
- Classic/SppServer ↔ Classic/SppClient、Classic/SppSerialServer ↔ Classic/SppSerialClient
- Classic/A2dpSource ↔ Classic/A2dpSink、Classic/HfpAudioGateway ↔ Classic/HfpHandsFree
- Classic/SppServer ↔ DualMode/ScanWhileSpp
- Info/ScanDumpとInfo/ConnectionInspectorは何に対しても使えます（他のexample、スマートフォン、市販のBLE機器）
