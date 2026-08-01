# Bluetooth Classic通信の入門ガイド

このガイドはBluetooth Classic（BR/EDR）の概念とAPI境界を説明します。
具体的なコードは[Classic examples](../examples/README.ja.md)で説明します。
BLEについては[BLE通信の入門ガイド](GUIDE_BLE_BASICS.ja.md)を参照してください。

## 1. BLEとは別の通信モデル

同じcontrollerを共有できますが、次の概念は統合しません。

```text
BLE Scan                   != Classic Inquiry
BLE Connection             != Classic ACL link
BLE GATT Client connection != SPP session
LE Pairing / Bond          != Classic pairing / link key
```

BLE APIはroot直下、Classic固有機能は`classic()`配下に置きます。どちらのcallbackも
`update()`から配送されます。

Bluetooth Classicは、Inquiryで機器を見つけ、必要ならACL linkを確立し、その上で
用途ごとのprofileを動かします。SPP、A2DP、HIDは同じClassic transportを使いますが、
データ形式と接続手順は異なるprofileです。

| | Bluetooth Classic | BLE |
|---|---|---|
| 周辺探索 | Inquiry | Advertising / Scan |
| 接続後の機能 | SPP、A2DP、HIDなどのprofile | GATT Service / Characteristic |
| Serial相当 | SPPのRFCOMM byte stream | GATT上に独自protocolを構築 |
| 保存する鍵 | Classic link key | LE bond key / IRK |

ClassicではBLEのCentral / Peripheralという役割名でprofile接続を説明しません。
SPPでは待受側をServer、接続を開始する側をClientと呼びます。このServer / Clientも
GATT Server / Clientとは別の概念です。

## 2. Capability

Classic Bluetoothを搭載する無印ESP32が対象です。利用前に
`EspBluedroidCapabilities`でInquiry、SPP、dual modeの可否を確認できます。個別profileは
`classic().profileSupport()`で、ライブラリ実装だけでなくCore設定や標準profileの有無まで
理由付きで確認できます。

capabilityは「SoC名から推測した値」ではなく、compile-time設定とlibraryが実装済みの
機能を合わせたsnapshotです。Classic対応SoCでも、build設定でprofileが無効なら
利用可能とは報告しません。

```cpp
const auto hid = bluetooth.classic().profileSupport(
  EspBluedroidClassicProfile::HidDevice);
Serial.println(hid.reason);
```

状態は`Supported`、`LibraryNotImplemented`、`CoreDisabled`、`CoreApiUnavailable`、
`NoStandardProfile`を区別します。[ProfileSupport example](../examples/Classic/ProfileSupport/README.ja.md)
では主要profileをまとめて表示します。

`EspBleBluedroid`がcontrollerとBluedroid hostのlifecycleを一括して所有します。
InquiryやSPPが個別にstackを初期化することはありません。このため、BLEとClassicを
同時利用しても二重初期化や、片方の終了がもう片方のstackを破棄する状態を避けられます。

## 3. Inquiry

InquiryはClassic機器の探索であり、BLE Scanとは別です。結果にはClassic address、
remote name、Class of Device、RSSIが含まれる場合があります。

探索される側はconnectableであるだけでなく、Inquiryへ応答するdiscoverable状態で
なければなりません。接続できる機器が常にInquiryで見つかるとは限りません。

結果フィールドの意味は次のとおりです。

| フィールド | 意味 |
|---|---|
| `address` | 48 bitのClassic device address |
| `name` | remote name。取得できない場合は空 |
| `classOfDevice` | Audio、Computerなど、機器カテゴリと提供機能のbit field |
| `rssi` | 受信強度。backendから得られない場合もある |

remote nameの取得はInquiry応答そのものより時間がかかる場合があります。名前だけへ
依存せず、addressやClass of Deviceも判断材料にします。

Inquiry開始の`true`は探索完了を意味しません。個々の結果は`onResult()`、終了は
`onComplete()`へ届きます。`stop()`した場合も完了eventが届き、`cancelled`で通常完了と
区別できます。callbackを処理できずqueueが満杯になった場合は
`droppedResultCount()`で観測できます。

関連example:

- [Inquiry](../examples/Classic/Inquiry/README.ja.md)

## 4. SPP

SPPはClassic上でbinary-safeなbyte streamを提供するprofileです。ServerとClientは
接続方向が異なりますが、接続後は共通のsession APIを使います。

SPPは内部でRFCOMMを使います。RFCOMMはSerial cableを模した信頼性のあるbyte streamで、
GATTのCharacteristic、MTU、Notifyとは関係ありません。接続前にClientがSDP
（Service Discovery Protocol）でServerのRFCOMM channelを探し、そのchannelへ接続します。

SPP sessionには`EspBluedroidSppSessionId`を使用し、BLEの
`EspBleConnectionId`は流用しません。現在はactiveなSPP sessionを1つに制限しています。

### 4.1 ServerとClient

Serverはservice name、RFCOMM channel、必要なSecurityを構成して待ち受けます。
channelを0にするとbackendが空いているchannelを選びます。Serverの開始完了とpeer接続は
別eventです。待受開始が成功してもsessionが存在するとは限りません。

Clientの`connect()`はSDPとRFCOMM接続を非同期に開始します。要求が受理されても、相手が
見つからない、SPPを公開していない、認証に失敗するなどの理由で後から失敗できます。
成立は`onConnected()`、失敗は`onConnectionFailed()`で受け取ります。

`EspBluedroidSppSession::incoming`はServerへ入ってきた接続か、Clientから開始した接続かを
示します。接続後のread/write APIを分ける条件にはしません。

### 4.2 Session API

session IDを明示するAPIは複数sessionへ拡張できるdata pathの正本です。接続、切断、
受信、送信完了、失敗は非同期eventとして配送されます。

受信にはpacket eventの`onData()`と、byte streamとして読む`available()` / `read()`の
両方があります。値はbinary-safeで、途中の`0x00`によって終端されません。公開RX ringは
2048 byte固定で、満杯時は既存byteを保持して超過分を捨てます。捨てたbyte数は
`droppedReceiveByteCount()`で確認できます。

送信要求は最大8件のqueueへ入り、1件は最大990 byteです。要求の`true`はqueueへ入った
ことを示し、peerへの送信完了は`onWriteCompleted()`へ届きます。queueが満杯の場合は
同期的に拒否し、`droppedWriteCount()`で累積を確認できます。

関連example:

- [SppServer](../examples/Classic/SppServer/README.ja.md)
- [SppClient](../examples/Classic/SppClient/README.ja.md)

### 4.3 Serial / Stream API

`EspBluedroidSppSerial`はrootへ自動attachされ、現在のServerまたはClient sessionへ
追従するArduino `Stream` / `Print`互換adapterです。`available()`、`read()`、
`write()`、`flush()`、`print()`、`println()`を利用できます。

constructorへrootを渡した時点でattachされるため、session callbackごとにbindし直す
必要はありません。切断中は`connected()`と`operator bool()`がfalseになり、
`available()`は0、`read()`は-1、`write()`は0を返してwrite errorを設定します。

大きな`write()`は990 byte以下へ分割してqueueへ入れます。戻り値は受理されたbyte数です。
`flush()`は選択中のsessionについて、受理済みwriteがbackendで完了するかsessionが切れる
まで待ちます。これは受信bufferを捨てる操作ではありません。

Serial adapterはsessionやstackを所有しません。必ず参照先の`EspBleBluedroid`より短く
生存させます。現在はactive sessionが1つなので自動選択できます。将来複数sessionへ
拡張した場合も、選択が一意なときだけ自動追従し、別peerのstreamへ途中で切り替えません。

関連example:

- [SppSerialServer](../examples/Classic/SppSerialServer/README.ja.md)
- [SppSerialClient](../examples/Classic/SppSerialClient/README.ja.md)

## 5. Securityとbond

Classic SecurityはBLE Securityと分離します。SSP Numeric Comparison、
DisplayOnly / KeyboardOnly Passkey Entry、authenticated/encrypted SPP、
Classic bondの列挙と削除を利用できます。

Classic側のI/O capabilityは、機器が数字を表示できるか、入力できるか、比較確認できるかを
表します。両端の組み合わせによってSSPの方式が決まります。

| capability | applicationに届く操作 |
|---|---|
| `None` | 確認UIなし |
| `DisplayOnly` | 6桁passkeyを表示 |
| `KeyboardOnly` | peerが表示した6桁passkeyを入力 |
| `DisplayYesNo` | 両端に同じ値を表示して一致を確認 |

表示、入力要求、比較確認、認証結果はいずれも`update()`から配送されます。入力・確認を
待つ方式では期限内に`providePasskey()`または`confirmNumericComparison()`で回答します。
未回答、明示拒否、値の不一致は接続失敗として扱います。

SPPのSecurity要求には認証なし、認証、認証＋暗号化があります。暗号化は盗聴を防ぎ、
認証は意図した相手かを確認します。用途が機密dataを含むなら
`AuthenticatedEncrypted`を選びます。

Classic bondはlink key、BLE bondはLE keyを管理します。片方の削除によって他方を
暗黙に削除しません。

関連example:

- [SppSecurity](../examples/Classic/SppSecurity/README.ja.md)
- [SppPasskey](../examples/Classic/SppPasskey/README.ja.md)

## 6. A2DP

A2DPは音楽向けの非同期audio profileです。受信する側がSink、送信する側がSourceです。
同じESPを状況に応じて両roleで使う場合もobjectを混ぜず、片方を`stop()`してからもう片方を
`start()`します。現在のCoreは同時に1 role、1 sessionだけを扱えます。

SinkはCoreがSBCから復号したPCMを受け取ります。

```cpp
auto &sink = bluetooth.classic().a2dpSink();
sink.onPcmData([](const EspBluedroidA2dpPcmData &pcm) {
  // pcm.dataをcallback中にbounded audio queueへcopyする
});
sink.start();
```

SourceはCoreから要求された容量までPCMを書き、`written`へ実際のbyte数を設定します。

```cpp
auto &source = bluetooth.classic().a2dpSource();
source.onPcmRequested([](EspBluedroidA2dpPcmRequest &request) {
  if (request.flush) return;
  request.written = audioQueue.read(request.data, request.capacity);
});
source.start();
source.connect("aa:bb:cc:dd:ee:ff");
```

PCMは16-bit interleavedで、formatの`sampleRate`、`channels`、`bytesPerSample`、
`bitsPerSample`はEspUsbHost/EspUsbDeviceの音声formatと同じfield名です。
Sinkのpointerはcallback終了後に保持できません。Sourceの`written`を`capacity`より大きくすると
その要求は0 byteとして拒否されます。`flush`時は`data == nullptr`であり、application側の
PCM queueやresampler状態を破棄します。

`onPcmData()`と`onPcmRequested()`だけはdeadlineのあるA2DP stack task上で同期実行されます。
一方、`onStarted()`、`onConnected()`、`onConnectionFailed()`、`onDisconnected()`、
`onStreamChanged()`は他のAPIと同様に`update()`から配送されます。

関連example:

- [A2dpSink](../examples/Classic/A2dpSink/README.ja.md)
- [A2dpSource](../examples/Classic/A2dpSource/README.ja.md)

### 6.1 AVRCP

AVRCPは再生操作とvolumeを扱い、audio dataは流しません。Core要件によりAVRCPをA2DPより
先に開始します。Controllerから1回の操作を送る場合は`click()`がPress/Releaseを送信します。

```cpp
auto &controller = bluetooth.classic().avrcpController();
controller.onConnected([](const EspBluedroidAvrcpConnection &) {
  bluetooth.classic().avrcpController().click(
    EspBluedroidAvrcpCommand::Play);
});
controller.start();
bluetooth.classic().a2dpSink().start();
```

操作を受ける側はTargetです。

```cpp
auto &target = bluetooth.classic().avrcpTarget();
target.onCommand([](const EspBluedroidAvrcpCommandEvent &event) {
  if (event.command == EspBluedroidAvrcpCommand::Pause &&
      event.state == EspBluedroidAvrcpKeyState::Released) {
    pausePlayback();
  }
});
target.start();
bluetooth.classic().a2dpSource().start();
```

Controller/Targetの接続はA2DP接続に追従し、独自の`connect()`は持ちません。callbackは
`update()`から配送されます。Target metadata/play-status応答はCore public APIがないため
未対応です。詳細は[対応表](CLASSIC_PROFILE_SUPPORT.ja.md#avrcpのcore制約)を参照してください。

## 7. BLEとの同時利用

dual modeではBLEとClassicを同時に利用できますが、radio、heap、callback queueは
共有資源です。SPP通信中も`update()`を短い間隔で呼び続けます。

同時利用できることは、任意の処理を無制限に並行できるという意味ではありません。
大量のSPP write、BLE Scan、GATT notification burstは互いにradio時間と処理時間を
奪います。drop count、write queue、受信ringを監視し、application側にもbounded queueを
設けます。必須機能はPSRAMなしで動作する容量を基準にしています。

`end()`はBLE connection、Classic Inquiry、SPP session、未配送eventを破棄するterminal
操作です。終了対象sessionの通常の切断callbackを待つ用途には使わず、applicationが
保持しているBLE connection IDとSPP session IDをすべて無効として扱います。

関連example:

- [ScanWhileSpp](../examples/DualMode/ScanWhileSpp/README.ja.md)

AVRCPとClassic HIDは現在の公開APIに含まれず、追加時も`classic()`配下の独立profileとして
BLE connectionやSPP sessionへ混ぜません。

主要profileの対応可否、Arduino-ESP32のbuild制約、実装優先度は
[Bluetooth Classic profile対応表](CLASSIC_PROFILE_SUPPORT.ja.md)を参照してください。
