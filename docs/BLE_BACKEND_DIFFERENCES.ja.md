# EspBle（NimBLE）とのBLE差分

この文書は、兄弟ライブラリ
[EspBle](https://github.com/tanakamasayuki/EspBle)とEspBleBluedroidの
**現在の公開APIと保証範囲**の差をまとめます。BLEの一般的な使い方は
[BLE通信の入門ガイド](GUIDE_BLE_BASICS.ja.md)、将来のAPI判断基準は
[API設計方針](API_DESIGN_POLICY.ja.md)を参照してください。

## 基本方針

backendが違っても同じ意味にできる機能は、型名、操作の要求、`update()`から届く
完了callbackまでEspBleへ揃えます。名前だけ同じで結果や完了条件が異なるAPIは
作りません。差は公開型、capability、戻り値、またはこの文書から確認できるように
します。

接続後のBLE操作は、backend objectをapplicationへ公開せず、
`EspBleConnectionId`と値型snapshotを使います。この境界により、Bluedroid固有の
イベント順や同期APIをlibrary内部で吸収します。一方、Bluetooth ClassicはBLEへ
混ぜず、`classic()`以下の別APIとして追加します。

## 現在揃っている範囲

| 領域 | 共通する使い方 | Bluedroid側の実装差 |
|---|---|---|
| lifecycle | `begin()` / `end()` / `update()` | BLEとClassicで共有するcontroller/hostをrootが一括所有 |
| Advertising / Scan | payload builder、scan result、開始・停止callback | Active ScanのAdvertisingとScan Responseの報告順が一定でないため、address単位で短時間merge |
| local identity / Tx Power | Advertising own address種別、`localAddress()`、`setTxPower()` | Random Static、controller RPA、実送信電力をESP-IDF GAP APIから直接制御 |
| Central接続 | 非同期`connect()`、connection ID、接続・切断callback | 現在は同時1接続。接続処理はworker taskへ隔離 |
| 接続パラメータ | snapshot、`updateConnectionParameters()`、`onConnectionParametersUpdated()` | 初期値はcontrollerから読み、GAP完了eventの合意値だけを`update()`から配送 |
| MTU | 既定247、`EspBleConnection::mtu`、`EspBleMtuChanged`、`onMtuChanged()` | 接続成立後にClientから明示的に交換を開始し、低レベル完了eventの合意値だけを公開 |
| 切断 | `disconnect()`と`onDisconnected()` | Arduino callbackが捨てるHCI reasonを低レベルGATTC eventから補完 |
| GATT Client | Discovery、Characteristic/Descriptor Read・Write、Subscribe | Bluedroidの同期wrapperを専用taskへ隔離し、完了は`update()`から配送 |
| BLE Security | Just Works、Passkey、Numeric Comparison、Bond | backend callbackへの同期回答は期限付きmailboxで受け、結果callbackとは分離 |

## 意図的に一致させていない範囲

### 接続と切断

EspBleは`disconnect(connectionId, reason)`でローカル切断理由を指定できます。
Arduino-ESP32 3.3.11のBluedroid `BLEClient::disconnect(reason)`は引数をlink終了処理へ
渡さないため、EspBleBluedroidは理由付きoverloadを公開していません。指定値を無視する
互換APIにはしません。受信した実際の理由は
`onDisconnected()`の`EspBleConnection::disconnectReason`へ格納し、取得できない場合だけ
0とします。

EspBleにある自動再接続とPHY更新は未実装です。Connection Parameter更新は同じ
公開APIで利用でき、初期snapshotと更新後の合意値を実機peerテストしています。
対象の無印ESP32はBLE 4.2 controllerで、LE 2M PHYとLE Coded PHYは利用できません。

### PeripheralとGATT Server

現在の公開APIはCentral 1接続とGATT Clientまでです。Peripheral connection snapshotと
GATT Serverは未実装です。GATT Server編のAPIをEspBle側で確定した後、静的定義、
permission、Read/Write、Notify/Indicateを同じ利用規則へ寄せます。Bluedroidが
NimBLEより動的変更しやすい場合でも、既存objectの寿命やcallback順を壊さない独立機能
として設計し、暗黙に有効化しません。

### GAPの未実装機能

Filter Accept List、Directed Advertising、接続開始時のパラメータ指定は未実装です。
Legacy Advertisingのown address選択、Random Static、RPA、送信電力設定には対応しています。
Scan Request側のown address typeはArduino wrapperがPublic固定のため、直接scan経路へ
置き換えるまで未対応です。ただしBluedroidの
公開APIはcontrollerが現在送信中のRPA値を取得できないため、RPA利用時の
`localAddress()`は空文字列を返します。接続後のパラメータ更新には対応しています。
Legacy Advertisingのみを扱います。無印ESP32のcontroller
制約によりExtended Advertising、Periodic Advertising、LE 2M/Coded PHYは追加できません。

## Bluedroidだから追加できる機能

Bluetooth ClassicはNimBLE backendのEspBleにはない機能です。現在は次を
`classic()`以下へ実装しています。

- Classic Inquiry
- SPP Server / Client
- active SPP sessionへ自動追従する`EspBluedroidSppSerial`
- SSP Numeric Comparison / Passkey EntryとClassic bond管理
- BLE Scan・GATT接続とSPP sessionの同時利用

今後Classic profileを追加する場合も、BLE connectionとClassic session、LE bondと
Classic link keyを別の型・別のresourceとして扱います。

Bluedroidの下位GAP/GATT eventやESP-IDF APIからArduino wrapperが公開しない情報・操作を
取得できる場合は、
BLE共通APIの意味を改善するために利用します。今回のMTU完了値とHCI切断理由がその例です。
private memberや内部object layoutには依存せず、Arduino-ESP32 3.3.11が公開するcallback、
リンク可能なESP-IDF API、完了eventをadapter内で使用します。wrapper非対応だけを理由に
機能を対象外にはしません。

## 対応を追加する条件

EspBleへ寄せる変更もBluedroid固有機能も、次を満たしてから「対応済み」とします。

1. 失敗するtestで公開する意味を先に固定する
2. generic `esp32`、PSRAMなしを前提にbuildできる
3. 無印ESP32 2台のpeerテストで要求、wire動作、完了、失敗、再接続を確認する
4. callbackがstack taskではなく`update()`から配送される
5. `end()`後に古いID、event、backend objectを残さない

実装済み範囲と実機試験名は[実装状況](STATUS.ja.md)で管理します。
