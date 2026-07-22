# EspBleBluedroid API設計方針

## 目的

EspBleBluedroidは、Arduino-ESP32のBluedroid stackを使う無印ESP32向けの
Bluetoothライブラリとする。BLE接続後の使い勝手は兄弟ライブラリEspBleへ
できる限り揃えつつ、Bluedroidで利用できるBluetooth Classic（BR/EDR）を
後から追加してもBLE APIが不自然にならない構造を採用する。

完全なソース互換は目標にしない。API名が同じでも意味や完了条件が異なる状態を
避けることを、表面的な一致より優先する。

この方針は2026-07-22時点のEspBle公開ヘッダーと設計文書、および
Arduino-ESP32 3.3.10同梱のBLE/BluetoothSerial APIと無印ESP32 build設定を
確認して作成した。具体的なsignatureは実装とPeerテストを追加する段階で確定する。

## EspBleから引き継ぐ設計

EspBleの次の性質はbackend固有ではなく、利用者にとって有用なため維持する。

- 1つのroot objectがstack lifecycleを所有する。
- Scanner、Advertising、GATT Server、Profileはrootから取得する非所有handleとする。
- BLE Central / PeripheralとGATT Client / Serverを同一視しない。
- 接続ごとにbackend handleとは別の安定したlibrary connection IDを発行する。
- stack callbackでは値をcopyしてqueueへ積み、利用者callbackは`update()`を呼んだ
  loop contextから配送する。
- 操作要求は原則non-blockingとし、同期戻り値は要求を受理したかだけを表す。
- 受理時の失敗は`bool`と`lastError()`、非同期完了はcontextを持つ結果イベントで
  報告する。
- GATT値はbyte sequenceを正とし、`String`はbinary-safeな便宜overloadに留める。
- ProfileはGATT定義とwire codecを提供するが、stackやAdvertising全体を所有しない。
- 公開APIへArduino-ESP32の`BLEClient`、`BluetoothSerial`、ESP-IDF handleを露出しない。
- 上限超過、queue overflow、timeoutを黙って成功扱いせず観測可能にする。

BLEの通常利用では、EspBleからの移植時にincludeとroot型を変更した後、接続後の
GATTコードをなるべく維持できる形を目標とする。

```cpp
#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;

void setup() {
  bluetooth.begin();
  bluetooth.onConnected([](const EspBleConnection &connection) {
    bluetooth.readCharacteristic(connection.id, serviceUuid, characteristicUuid);
  });
}

void loop() {
  bluetooth.update();
}
```

この例の`onConnected()`、`EspBleConnection`、GATT操作、結果callbackの使い方は
EspBleと同じ意味へ揃える。Scan開始から接続成立までの挙動、Security、MTUなど
backend差が大きい箇所は、同じsignatureを無理に維持しない。

## BLEとClassicの境界

root objectはBluedroid host/controller全体を所有するが、公開モデルではBLE linkと
Classic profile sessionを分離する。

BLEの既存形はEspBleに近いフラットなAPIとして残す。

```cpp
bluetooth.scanner();
bluetooth.advertising();
bluetooth.connect(bleScanResult);
bluetooth.gattServer();
bluetooth.hidHost();       // HID over GATT
bluetooth.hidKeyboard();   // HID over GATT
```

Classic固有機能は`classic()` facadeの下へ配置する。

```cpp
auto &classic = bluetooth.classic();
classic.inquiry().start();
classic.spp();
// 将来候補: classic.hidHost(), classic.a2dpSink(), classic.avrcpController()
```

この非対称性は意図的である。root直下のBLE APIを`ble()`配下へ移すとEspBleとの
移植性が下がる一方、ClassicのInquiryやprofile接続をrootの`scanner()`や
`connect()`へ混ぜると意味が曖昧になるためである。

次の概念は統合しない。

```text
BLE Connection             != Classic ACL link
BLE GATT Client connection != SPP session
BLE HID over GATT          != Classic HID
LE Pairing/Bond            != Classic pairing/link key
BLE Scan                   != Classic Inquiry
```

Classic側はprofile session単位のIDを持つ。初期SPPなら
`EspBluedroidSppSessionId`を使い、`EspBleConnectionId`を流用しない。将来、複数の
Classic profileが同じACL linkを共有する必要が生じた場合だけ、内部のClassic link ID
またはtransport付き共通link IDを追加する。先に公開して推測的な共通化はしない。

## 所有権とライフサイクル

`EspBleBluedroid`だけがcontrollerとBluedroid hostを初期化・終了する。
BLE機能やClassic profileが個別に`BLEDevice::init()`、`BluetoothSerial::begin()`、
ESP-IDFのcontroller初期化を行ってはならない。

構成手順は次を基本とする。

1. root objectを作る。
2. BLE GATT/ProfileとClassic Profileを構成する。
3. `begin(config)`で必要なmodeとresourceを確定してstackを開始する。
4. Advertising、Scan、Inquiry、profile接続を開始する。
5. loopから`update()`を継続して呼ぶ。
6. `end()`で全session、link、profile、stackの順に終了する。

`begin()`後にprofileを追加できるかはtransport/profileごとのcapabilityとする。
EspBleの「GATT Server定義はbegin前に固定」という利用規則は初期版でも維持するが、
その根拠をNimBLEの制約からコピーせず、Bluedroidで再検証して確定する。

ClassicとBLEを同時に有効化できることと、任意の操作を無制限に並行実行できることは
別である。初期化modeは少なくとも次を区別できる内部設計とする。

- BLE only
- Classic only
- Dual mode

公開configでmodeを明示させるか、構成済み機能から自動決定するかは実装時に
メモリ計測して決める。どちらの場合も利用できない組合せを黙って縮退させず、
`begin()`を明示的に失敗させる。

必須機能は内部RAMだけで動作させ、PSRAMを前提にしない。同じESP32 SoC内のmodule・
flash・PSRAM有無は通常のbuild matrixでは増やさず、generic `esp32` profileを基準にする。
将来、大容量queueなどへPSRAMを任意利用する場合は、PSRAMなしのfallbackを維持し、
その機能固有のresource testで検証する。

## 状態モデル

状態をrootの単一enumへ押し込まない。

- Stack lifecycle: uninitialized / initializing / ready / stopping / error
- BLE Scanner、Advertising、BLE Connection: EspBleと同等の独立状態
- Classic Inquiry: idle / inquiring / stopping
- Classic profile: profileごとのunconfigured / ready / stopping / error
- Classic session: connecting / connected / disconnecting / disconnected

BLE Advertising中にClassic SPP sessionが存在する、といったdual-mode状態を表現
できることが必要である。集約された`connected()`だけをrootへ設けず、接続数と状態は
BLE ConnectionまたはClassic profileごとに問い合わせる。

## イベントと非同期操作

BLE/Classicの通常callbackはすべて同じ`bluetooth.update()`から配送する。
ESP-IDF/Bluedroid callback taskから利用者コードを直接呼ばない。

イベントには少なくとも次を値として保持する。

- transportまたはprofile種別
- libraryが発行したConnection/Session ID
- peer address
- 成否、library error、backend由来detail
- 操作対象（GATT UUID/handle、SPP sessionなど）
- copy済みpayload、またはcallback中だけ有効であることを明示したview

queueはBLEとClassicで無条件に1本へまとめない。大量のSPP受信や音声dataが、切断・
Security・GATT完了イベントを追い出さないよう、制御イベントとstream dataに別の
backpressure方針を持たせる。lifecycleと操作完了イベントを優先保持し、drop数は
transport/profile別に取得できるようにする。

SPPなどのstream dataは`update()` eventだけでは帯域不足になる可能性がある。
初期SPPはArduino `Stream`互換のread/writeと、`update()`配送の状態イベントを分ける
案を優先する。zero-copy/raw callbackが必要になった場合はadvanced APIとし、stack
contextまたは専用task contextであることを型名と文書に明記する。

## Errorとcapability

BLE共通面のerror分類はEspBleの`None`、`InvalidState`、`InvalidArgument`、
`BackendFailure`、`ResourceExhausted`、`NotFound`、`Timeout`を初期集合とする。
Classic追加だけを理由に既存値の意味を変えない。未対応機能を区別する必要が確認
できた時点で`Unsupported`等を末尾追加し、backend error codeは`detail`へ保持する。

SoC名だけで機能を推測しない。compile-time設定と実際に初期化したmodeから得た
capability snapshotを用意する。少なくとも次を区別する。

- BLE / Classic / dual-modeの利用可否
- GATT Client / Server、LE Security
- SPP、Classic HID、A2DP、AVRCP、HFPなどprofileごとの利用可否
- buildで決まる接続数、GATT attribute、notification登録などの上限

Arduino-ESP32 3.3.10の現在の無印ESP32 buildでは、Bluedroid、BLE GATT、SPP、
A2DP、AVRCP、HFPが有効で、BLE最大接続数、Classic ACL数、GATT attribute数などに
build-time上限がある。ただし、これらの値をEspBleBluedroidの保証値として固定しない。
Core更新、同時利用profile、heap残量で実用上限が変わるため、capability/diagnosticと
実機テストで扱う。

## BluedroidによるBLE差分の扱い

Arduino-ESP32 3.3.10の同梱BLE APIはBluedroid/NimBLEで共通化されているが、完全に
同一ではない。少なくとも次をbackend adapterで吸収または明示する。

| 領域 | 方針 |
|---|---|
| Scan/Connect | callback順、停止完了、接続処理のblocking性をPeerで確認し、公開操作はEspBle同様に要求と完了を分離する |
| Subscribe | NimBLE専用`subscribe()`へ依存せず、Bluedroidのnotify登録とCCCD書込みをadapterで一つの非同期操作にする |
| Notify/Write順 | notificationが同期write戻りより先に到着し得るため、相互に因果関係のないcallback順を公開保証しない |
| Security permission | NimBLEのproperty flagを流用せず、Bluedroidのaccess permissionへ明示変換する |
| MTU | role別callbackと交換開始条件を実機確認し、合意MTUだけをConnection snapshotへ公開する |
| Bond/Security | LEとClassicのstore、認証状態、callback contextを分離する。単一のbond一覧へ混在させない |
| Discovery cache | backend objectを公開せず、EspBleと同じ固定容量のlibrary snapshotへcopyする |
| Descriptor context | callbackにConnection情報が不足する場合、推測で埋めず、ESP-IDF event利用またはcontextなしの型で表す |
| Dynamic GATT | begin後のService追加可否はBluedroidで独立に検証し、初期版では禁止する |

同名APIを採用する条件は「コンパイルできる」ことではなく、成功・失敗・完了callback・
timeout・値の寿命まで同じ意味をPeerテストで確認できることである。

## API互換性の分類

EspBleのAPIを移植するときは、各機能を次の3分類へ明示する。

### 同等API

名前、引数、結果、非同期完了条件まで同じ意味を提供できるもの。

- `begin()` / `end()` / `update()`
- BLE `scanner()` / `advertising()`
- BLE Connection IDとConnection snapshot
- Generic GATT read/write/discovery/subscribe
- GATT Serverの静的構成
- backend非依存のcodec、keymap、HID Report Map解析

### 類似API

利用形は似せられるが、結果フィールド、保証順序、設定可能範囲などが異なるもの。

- Scanから接続するまでの制御
- MTU、接続パラメータ、PHY
- Pairing、passkey、Numeric Comparison、Bond管理
- 複数接続上限と同時GATT操作数
- HID Host/Deviceのbackend依存部分

差は型、capability、戻り値または文書で見えるようにし、黙って無視しない。

### 別API

BLEに等価概念がなく、Classic profileとして独立させるもの。

- Classic Inquiry
- SPP
- Classic HID
- A2DP / AVRCP
- HFP

## Classic機能の追加順

Classic対応は「buildで有効だから一括公開」せず、profileごとに設計・Peerテストを
完了して追加する。

1. Classic capabilityとInquiry。BLE Scanとの差を確定する。
2. SPP。Client/Server session、双方向data、切断、再接続、Securityを検証する。
3. BLEとSPPのdual-mode同時利用。resource競合とevent starvationを検証する。
4. Classic HID Host/Device。HOGPと共有できるusage/report codecだけを共通化する。
5. A2DP/AVRCP。audio data pathとcontrol eventを分離して設計する。
6. HFP。同期音声linkを含むため、別途resource/latency設計を行う。

最初のClassic公開対象はSPPを推奨する。Arduinoの`Stream`利用形に馴染みがあり、2台の
無印ESP32だけで接続、data、Security、再接続を自動検証できるためである。ただし
Arduino-ESP32の`BluetoothSerial`型は公開APIに採用しない。同型はdeprecated表示が
あり、将来のCore変更へ追従できるよう内部backendまたはESP-IDF APIへ閉じ込める。

## 共有コードの方針

EspBleの実装ファイルを都度copyして分岐させない。次はbackend非依存候補として、
テストと一緒に共有可能な単位へ切り出す。

- keymapとHID usage変換
- HID Report Map parserとreport codec
- BLE MIDI、医療float、CRCなどのwire codec
- UUID正規化とbyte sequence helper
- backend型を含まないConnection/GATT/Profileの値型

共有化は別ライブラリ依存を直ちに増やす意味ではない。まず両実装で必要な境界を
確認し、同じunit testを通せるコードだけを共有する。stack所有、接続、Security、
GATT object寿命、callback adapterはbackend固有として分離する。

両ライブラリを同じsketchへincludeすることは想定しないが、Arduino環境へ同時に
installしてもheader解決が衝突しないよう、root headerとroot classは
`EspBleBluedroid`固有名にする。BLE値型の完全共有を行う場合は、双方が同じ共通header
を参照する形にして二重定義を避ける。

## テストをAPI確定条件にする

新しい公開APIは、対応するテストなしに「EspBle互換」または「対応済み」としない。
実装手順は[DEVELOPMENT.ja.md](DEVELOPMENT.ja.md)のRed-Green-Refactorに従う。

- BLE共通面: EspBleと同じscenario名・wire期待値を可能な範囲で再利用する。
- backend差: callback順、timeout、切断理由、MTU、Securityを両端Serialで確認する。
- Classic: profileごとに接続側/待受側、data両方向、切断、再接続、認証失敗を確認する。
- Dual mode: BLE GATT通信中のSPP trafficなど、同時利用を別testとして確認する。
- lifecycle: `begin()`/`end()`、接続/切断反復でheap、task、bondをリークしない。
- resource上限: 上限値自体を固定仕様にせず、超過時の明示エラーを確認する。

現在の`stack_smoke`はBluedroid BLEのAdvertising、Scan、接続、GATT read/write、
CCCD購読、notificationまでを確認済みである。公開APIでは`advertise_scan`により
lifecycle、Advertising、Scan、`update()`からのcallback配送を確認済みである。
詳細は[実装状況](STATUS.ja.md)を参照する。backend smokeの成立だけではEspBle互換APIの
確定を意味しない。

## 当面確定しない事項

次はPeer試作とresource計測なしに固定しない。

- root configでBLE/Classic modeを明示するか自動決定するか
- BLEとClassicを合わせた総link数と各profile session上限
- Classic共通link IDの公開要否
- SPPのbuffer容量、blocking write、zero-copy callback
- Classic pairing UIとLE Security callbackをどこまで共通化するか
- Dynamic GATT Service追加
- A2DP/HFPのaudio buffer所有権と実行context
- EspBleとの共通headerを別packageにする時期

これらを未確定のままにすることは、将来拡張をrootの曖昧な`connect()`や単一
`connected()`へ押し込まないための意図的な判断である。
