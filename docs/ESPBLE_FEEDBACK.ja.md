# EspBle側への修正要望

このライブラリを実装・テストする過程で見つかった、**EspBle（NimBLE版、兄弟ライブラリ）側で
直すべき／揃えるべき事項**の記録です。こちら側の未実装や後端（Bluedroid）由来の制約は
対象外で、それらは[STATUS.ja.md](STATUS.ja.md)・[docs/API_PARITY.tsv](API_PARITY.tsv)・
[examples/DIFFERENCES_FROM_ESPBLE.md](../examples/DIFFERENCES_FROM_ESPBLE.md)にあります。

なぜ別ファイルにするか。`tests/interop/`の規則で
「installされたpackageへpatchを当ててテストを通すことはしない。EspBle側を直さないと通らない
場合はその事実を結果に残す」と決めています。その「残す」先がここです。相互接続テストは
**片方の実装しか埋めていないfieldではなく、仕様が両者に要求する一致**をassertするので、
片側だけの挙動は期待値から外し、代わりに項目としてここへ書きます。

書き方の規則:

- 対象versionを明記する。以下はすべて**EspBle 1.1.0**（`tests/interop/*/sketch.yaml`が
  pinしているrelease、および同一versionのdev tree `/home/mt/dev/EspBle`）で確認したもの。
- 「気になる」ではなく**再現手順か該当箇所**を書く。直す人が探し直さなくてよい状態にする。
- 直す／直さないの判断はEspBle側の裁量。ここは要望の一覧で、決定の記録ではない。
- 修正が入ったら、pin versionを上げた時点でこちら側の回避（テストの例外、DIFFERENCESの行）
  も一緒に外す。外し忘れを防ぐため、各項目に「解消時にこちらで消すもの」を書く。

## 1. HID Host: keyboard eventの`rawData` / `rawLength`が空

**状態**: 未報告 / 実機で確認済み（`tests/interop/hid`）
**影響**: 小（移植時の落とし穴。仕様上の相互運用性には影響しない）

`EspBleHidKeyboardEvent`は`EspBleHidReport`を継承していて`rawData` / `rawLength`を持ちますが、
keyboard eventだけこれが埋まらず`nullptr` / `0`のまま配送されます。同じHID Hostの
Mouse・ConsumerControl・SystemControl・Gamepad・Vendorは**すべて埋めている**ので、
keyboardだけが例外です。

該当箇所（EspBle 1.1.0）:

| 場所 | 内容 |
|---|---|
| `src/EspBle.h:781-786` | `EspBleHidReport`が`rawData` / `rawLength`を宣言 |
| `src/EspBle.cpp:4240` 付近 | keyboard reportを`EventType::State`としてqueueする箇所。`event.state`は埋めるが、同じ`Event`構造体が持つ`raw[64]` / `rawLength`は触らない |
| `src/EspBle.cpp:8678` 付近 | `EspBleHidKeyboardEvent keyboardEvent;`を組み立てて配送する箇所。`rawData` / `rawLength`への代入がない |
| `src/EspBle.cpp:8714` 付近 | 比較用。Mouse以降は`value.rawData = event.raw; value.rawLength = event.rawLength;`をしている |

修正は2箇所への代入で足ります。`Event`は`state`と`raw`の両方を持っているので、構造体の変更も
バッファの追加も不要です。

```cpp
// queue側（EventType::State を立てている場所）
event.rawLength = length;
memcpy(event.raw, data, length);

// 配送側（keyboardEvent を組み立てている場所）
keyboardEvent.rawData = event.raw;
keyboardEvent.rawLength = event.rawLength;
```

注意点2つ:

- **1つのreportから複数のkeyboard eventが出ます**（変化したusageごとに1件）。その全件が同じ
  reportを指すことになります。これは意図どおりで、このライブラリも同じです。生reportは
  「このeventの元になったreport」であって「このeventだけの由来」ではありません。
- `EspBleHidKeyboardState`（`onKeyboardState`が受け取る型）には`rawData`系のfieldが
  そもそも無いので、状態通知側で生reportを見ることは今のAPIではできません。こちらも同じ形なので
  **差分ではありません**が、揃えて足すかどうかは設計判断として残ります。足すなら両方で同時に。

**確認した内容**: `tests/interop/hid`のEspBle host方向で、6KROの8 byte reportを受けた
keyboard eventが`length=0`。同じreportをこちら側のhostが受けると`length=8`。

**解消時にこちらで消すもの**:

- `tests/interop/hid/test_hid.py`の`check_keystroke(..., raw_length=False)`と、その引数自体
- `examples/DIFFERENCES_FROM_ESPBLE.md` / `.ja.md`の「HID Host keyboard events」の行

## 2. 共有headerの整形差（両ライブラリで同時に揃える必要がある）

**状態**: 未報告 / 差分は`diff`で確認済み
**影響**: 小（動作は同一）。ただし**共有headerの仕組み自体を無効化している**

両ライブラリで共有しているheaderのうち、こちらの複製は整形が変わっています。中身は同一で、
生成するbyte列も同じです。

| header | 差分 | 内容 |
|---|---|---|
| `EspBleUuid.h` | 0行 | 完全一致 |
| `EspBleHidReportMap.h` | 共有注記のみ | 一致 |
| `EspBleMidi.h` | 共有注記のみ | 一致 |
| `EspBleKeymap.h` | 共有注記のみ | 一致 |
| `EspBleCgmCrc.h` | 83行 | brace省略の展開、hex小文字化、loop変数名、コメント文言 |
| `EspBleMedicalFloat.h` | 170行 | 同上、および`switch`内のindent |
| `EspBleIBeacon.h` | 108行 | 同上。加えて**説明コメントが削られている** |

問題は行数そのものではなく、共有headerの先頭に自分で書いた注記が

> Everything below this note is a verbatim copy of EspBle's file, so both libraries put
> the same bytes on the wire and any drift shows up as a plain diff.

と言っている一方で、実際の`diff`が100行前後の整形ノイズで埋まっていることです。この状態では
**意味のあるdrift（片側だけのバグ修正、片側だけの仕様追随）が埋もれて見つかりません**。
共有headerを`diff`で守るという設計が、その3ファイルについては機能していません。

解消の方向は2つあり、どちらでもよいがどちらかに決める必要があります。

| 方向 | 内容 | 代償 |
|---|---|---|
| EspBle側を整形に揃える | 3ファイルをこちらの整形へ寄せる | EspBle側に動作の変わらないcommitが1つ増える |
| こちら側を戻す | 3ファイルをEspBle 1.1.0のまま複製し直す | このリポジトリの整形規則に沿わない箇所が残る。`EspBleIBeacon.h`の削られたコメントは復活する |

`EspBleIBeacon.h`については、削られたコメント（company IDのbyte順、`0x15`が何を数えた長さか、
`measuredPower`が1 m地点の校正RSSIであること）は**読む価値のある情報**なので、どちらの方向を
採るにしても復活させるのがよいと考えます。

**解消時にこちらで消すもの**: なし（`diff`が0行になることが結果）。

## 3. 参考: 6KROのerror codeの扱いが両実装で違う（EspBle側が正しい）

**状態**: **EspBle側の修正は不要**。こちら側の修正項目として記録
**影響**: 小

boot互換レイアウト（`[modifiers, reserved, keycode1..6]`）のkey配列に入りうる`0x01`〜`0x03`は
キーではなくerror codeです（`ErrorRollOver` / `POSTFail` / `ErrorUndefined`）。

- EspBle（`src/EspBle.cpp:4233` 付近）は、6つのslotの**どこかに**`0x01`〜`0x03`があれば
  そのreportを丸ごと捨てます。
- こちらは`0x01`が**2つ以上**のときだけrolloverとして前の状態を維持し、`0x02` / `0x03`は
  usage 2 / usage 3の押下として配送してしまいます。

存在しないキーを押されたと報告する方が明確に悪いので、これはこちら側の不具合です。EspBle側へ
要望することはありません。この項目は「相互接続で気づいた差のうち、直すべきなのはこちらだった」
記録として残します。

**対応済み**: `src/EspBleBluedroidHidHost.cpp`の`dispatchKeyboard()`で、key slotに`0x01`〜`0x03`が
1つでもあれば`invalidInputReportCount()`を増やして前の状態を維持するようにしました（error codeを
bitmapへ立てない）。実機確認は`tests/peer/hid_boot_protocol`が`0x01`のrolloverを見ており、
`0x02` / `0x03`は**未検証**です。検証するなら、device側sketchから`keys[0] = 0x02`の生reportを
送るコマンドを`tests/peer/hid_keyboard_host`へ足すのが最短です。

## 4. `examples/Hid/KeyboardHost/README`の期待出力が実際の順序と違う

**状態**: 未報告 / 実装を読んで確認
**影響**: 極小（ドキュメントのみ）

EspBleの`examples/Hid/KeyboardHost/README.md` / `.ja.md`の「期待されるSerial出力」が

```
Key pressed: usage=0x04 ascii=0x61
Keyboard state: modifiers=0x02 A=1 pressed=1 released=0
```

となっていますが、実装（`src/EspBle.cpp:8633`付近、`EventType::State`の分岐）は
**state callbackを先に呼び、そのあとにusage単位のkeyboard event**を配送します。順序が逆です。
加えて`modifiers=0x02`（Shift）と同じ入力の`ascii=0x61`（`a`）も噛み合っていません。Shiftを
伴えば`0x41`です。

こちらの`examples/Hid/KeyboardHost/README`は正しい順序で書いてあります（同じ入力から
modifier単独のeventも出ることも併記）。

**解消時にこちらで消すもの**: なし。

## 対象外のもの

次は**要望ではありません**。混ぜると一覧が願望リストになるので明示しておきます。

- EspBleにしか無いAPI（`updatePhy()`、`setAutoReconnect()`、`disconnect(id, reason)`など）。
  こちらの未実装または後端の制約で、分類は[API_PARITY.tsv](API_PARITY.tsv)にあります。
- Bluetooth ClassicがEspBleに無いこと。無印ESP32だけが持つ機能で、EspBleの対象SoCには
  そもそもClassicの無線がありません。
- 相互接続で観測したBluedroid固有の挙動（接続時MTU交換、PDUごとのscan event、RPAの非公開）。
  こちら側の事情で、[BLE_BACKEND_DIFFERENCES.ja.md](BLE_BACKEND_DIFFERENCES.ja.md)にあります。
