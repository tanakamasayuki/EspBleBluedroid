# Peer Tests

無印ESP32 2台をBLEで接続する自動テストです。ボード間の信号配線は不要です。

| pytest上の位置 | profile | 環境変数 | 現在のBLE role |
|---|---|---|---|
| 親側 | `esp32_peer_host` | `TEST_SERIAL_PORT_ESP32_PEER_HOST` | Central |
| `peer_device/` | `esp32_peer_device` | `TEST_SERIAL_PORT_PEER_DEVICE_ESP32_PEER_DEVICE` | Peripheral |

`stack_smoke`はライブラリの公開APIには依存せず、Arduino-ESP32同梱Bluedroid
APIだけで接続し、テスト環境と基本GATT data pathを検証します。

`advertise_scan`はEspBleBluedroidの公開APIを使い、lifecycle、Advertising、31 byte
超過の拒否、Scan resultと、利用者callbackが`update()`から配送されることを検証します。

`advertise_payload`は親側でraw PDUを解析し、複数Service UUIDのAD構造、31 byte境界、
時間指定停止を検証します。

`connect_disconnect`は公開APIによるCentral接続、再接続ごとのconnection ID、
到達不能peerの非同期失敗、callbackの`update()`配送、切断、stack再初期化を検証します。

`classic_inquiry`はdiscoverableなClassic-only peerに対するcapability snapshot、
Inquiry result、結果callbackからの停止、`update()`上の完了eventを検証します。

`spp_server`はraw ESP-IDF Clientに対する公開SPP Server、binary data、8件送信queue、
overflow、再接続ID、remote切断、stack終了を検証します。

`spp_client`はraw ESP-IDF Serverへの公開非同期Client接続、共通session、local切断、
再接続、失敗eventを検証します。

`spp_multi_backend`は公開APIに依存しないraw Bluedroidの成立性試験です。同一ACL上の
異なる2つのRFCOMM server channelへ同時接続し、handle別の双方向dataと両sessionの
切断を検証します。同じchannelへの2本目や公開APIの複数session対応を保証する試験では
ありません。

`dual_mode_scan_spp`は1つのdual-mode stack上でBLE Scanとbinary SPP trafficを
同時利用できることを検証します。
`multi_listener`は多重observer配送を検証します。primaryの`on*()`に加えてconnection系・
GATT Client系・GATT Server系eventの`add*Listener()`が登録順に配送されること、1 event
4件の上限、owner単位のlistener id、dispatch中に追加したlistenerがそのdispatchに
含まれないことを確認します。
`peripheral_connection`はraw Arduino-ESP32 BLE client相手に、Peripheral側の接続
lifecycleを検証します。接続event、こちらが観測するだけのMTU交換、パラメータを含む
`connection()` snapshot、その役割でのpairing、HCI切断理由、GATT Server eventが持つ
connection IDの一致を確認します。
`midi_device`と`midi_host`は、BLE MIDI profile helper（`EspBleMidiProfile.h`）を
Device・Host両方の役割で検証します。相手はraw Arduino-ESP32で、BLE MIDIのヘッダを
自前の演算で組み立て・デコードします。したがって同じcodecを両端で突き合わせるのではなく、
仕様と突き合わせることになります。timestamp header、packetを越えて持ち越されるrunning
status、間に挟まったSystem Real-Time byte、複数packetに分かれるSysExを両方向で確認し、
転送中の2本目が拒否されることも固定します。BLE MIDIのUUIDは仕様で固定されているため、
この2つのsuiteはsuite UUID tagではなくデバイス名で隔離します。
`duplicate_uuid_server`は、同一Service内で同一UUIDを共有するCharacteristic 2件を
本ライブラリが公開したとき、電波上に実体が2つあることを検証します。raw Arduino-ESP32の
Centralがhandle keyのmapを辿り（wrapperのUUID key mapは重複の一方しか返せません）、
2つの値、2つのReport Reference相当のDescriptor、2つのCCCDを読み出します。2件目のhandleへの
Writeは2件目のCharacteristicへ帰属し、Notificationは送信元handleへ届きます。これはHID over
GATTの前提です（キーボードのReport Characteristicはすべて0x2a4dを持つため）。
`hid_keyboard_device`は、HID over GATTのkeyboardをhost OS役のraw Arduino-ESP32
Central相手に検証します。Report Mapのlong read（既定MTU）が`tests/unit/hid_report_maps`で
固定したdescriptorと完全一致すること、2件の0x2A4D Report CharacteristicがReport Reference
descriptorで区別できること、Input Report Notificationが8 byteのkeyboard layoutを運ぶこと、
HostのLED writeが`onOutputReport()`と`ledState()`に返ること、Protocol Mode writeが
報告されること、Device InformationとBatteryが`configure()`に渡した値であることを確認します。
呼び出し側が区別する必要のある2つの拒否理由（`no connected HID Host`と
`no subscribed HID Host`）、および切断後に`ready()`もLED状態も残らないことも固定します。
`hid_composite`は、1つのHID serviceを共有する5つのDevice profile（keyboard、mouse、
consumer control、system control、gamepad）を検証します。composite機器でしか壊れない点を
確認します。公開されるReport Mapが5つのdescriptorをprofile順に連結し、マウスのボタン数を
埋め込んだものであること（期待値は`unit/hid_report_maps`と同じsnapshotから組み立てるので、
合成規則そのものがassertionになります）、5本のInput Report characteristicがUUID 0x2A4Dを
共有しReport Referenceを各々持つこと、各Notificationが送信元profileのhandleへ正しいバイト列で
届くこと。属性の並び順（configure順）とReport Map内のdescriptor順（profile順）は意図的に
異なります。hostが使うのはどちらでもなくReport Referenceだからです。

`hid_boot_protocol`はHID over GATT Boot Protocol——Report Descriptorを解釈できない
Hostが使う固定8 byteのkeyboard report——を検証します。keyboardをNKROにしているので両modeの
差は最大で、変換そのものが主題です。同じ`sendReport()`が、Report Protocol ModeではReport Mapが
宣言する29 byteのbitmapとして、Boot Protocol Modeでは`[modifiers, reserved, keycode1..6]`として、
別のhandleへ出ます。期待値はtest側がusageから両形式で組み立てるので、同じ変換を2回突き合わせて
いません。6キーを超える保持はHIDのrolloverコード0x01が全slotに入ります。boot Hostには任意の
部分集合ではなく「多すぎる」と伝える必要があるためです。Hostは**両方**のInput Reportを購読するので、
どちらがキー入力を運ぶかはデバイスの判断です。LED writeはBoot Keyboard Output Reportへ行き、
同じ`onOutputReport()`に届く必要があります。そして`ready()`は生きているreportのCCCDに従います。
Boot Protocol Modeでboot CCCDを切ると、Report-protocol側の購読が残っていても`ready()`はfalseで
送信は`InvalidState`で失敗します。

`hid_vendor_custom`は、ライブラリが中身を解釈しない2つのprofileを検証します。descriptorは
固定でReportサイズだけが利用者指定の`hidVendor()`と、descriptor自体が利用者のものである
`hidCustom()`です。Hostから書き込まれる唯一のprofileなので、`hid_composite`では扱えない
方向を確認します。Report Mapが「合成された内蔵descriptor＋sketch自身のdescriptor」で
あること（期待値はここでも同じsnapshotから組み立て、vendorのサイズは既定値以外にして
埋め込みが電波上に現れるようにしています）、有効な内蔵profileが占めるreport IDが
`hidCustom()`に対して拒否されること、6件のReport characteristicがUUID 0x2A4Dを共有し
そのwrite属性がReport Referenceのtypeと一致すること（Inputはnotify、Outputはwrite without
responseも可、Featureは設定情報なので常に応答つき）、HostのOutput・Feature Reportが
呼び出し側の`update()`からバイト単位でcallbackへ届くこと。Reportは40 byteで既定MTUの
ATT payloadには収まらないため、バイト列を確認する前にデバイス自身が見た交渉後MTUを
assertします。そうしないと切り詰めと誤ったReportが見分けられません。拒否理由も固定します:
宣言と異なる長さは`InvalidArgument`、宣言していないreport IDはcharacteristicを勝手に作らず
`NotFound`です。
