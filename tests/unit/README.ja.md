# Unit Tests

> English: [README.md](README.md)
> 位置づけ: [../TEST_PLAN.ja.md](../TEST_PLAN.ja.md)

host上で実行する純粋C++/データ変換のテストです。実機とシリアルポートは不要で、g++で
ビルドして実行します。

```sh
uv run --env-file .env pytest unit/
```

1 suiteが1 directoryで、C++のテストプログラムと、それを`-Wall -Wextra -Werror`で
コンパイルして実行するpytest wrapperを置きます。非0終了で失敗とします。

## 追加済み

- `uuid`: `src/EspBleUuid.h`のUUID codecを検証する。16/32/128-bitのparse・format、
  短縮形、Bluetooth Base UUIDとの等価性を扱う。
- `codec`: `src/internal/EspBleBluedroidCodec.cpp`のBluedroid固有変換と、
  `src/internal/EspBleBluedroidGattcState.cpp`のGATT Client link状態機械を検証する。
  backend内部の状態を扱うため、EspBleに対応するsuiteは存在しない。
- `ibeacon`: `src/EspBleIBeacon.h`のiBeacon codecを検証する。manufacturer payloadの
  全フィールドencode/decodeを扱う。
- `medical_float`: `src/EspBleMedicalFloat.h`のIEEE-11073 medical float codecを検証する。
  32-bit FLOATと16-bit SFLOATのencode/decode round trip、正確なlittle-endianバイト配置、
  負のmantissa、Health Thermometer / Blood Pressure / Glucoseで使う予約値
  （NaN / NRes / ±INFINITY）を扱う。
- `cgm_crc`: `src/EspBleCgmCrc.h`のCGM E2E-CRC codec（CRC-16/MCRF4XX）を検証する。
  「123456789」の既知チェック値0x6f91、空入力の初期値、代表的なCGM Measurementに対する
  append/verify round trip、破損検出、CRCを収められない短すぎる値の拒否を扱う。
- `keymap`: `src/EspBleKeymap.h`のHID usage→文字変換
  （`espBleUsageToUnicode` / `espBleUsageToAscii`）を、各layoutの一次ソース
  （Windows layoutデータ）由来の期待値で検証する。AltGr層の選択とfallback、
  文字ペア判定CapsLock、dead key→0、非Latin-1文字の`ascii`=0を固定する。
- `report_map`: `src/EspBleHidReportMap.h`のHID Report Map parserを検証する。
  項目順序が異なるkeyboard、Report IDなしのboot keyboard、Consumer Control併載機、
  mouse-only descriptorや途中で切れたdescriptorを扱う。
- `midi`: `src/EspBleMidi.h`のBLE MIDI packet codecを検証する。timestamp header/
  lowバイトのデコード、running status（timestampバイト有無の両方）、
  System Real-Time割り込み、単一/2パケットのSystem Exclusive、異常系パケット、
  packet builder、複数パケットSysEx encoderを扱う。
- `hid_report_maps`: 本ライブラリが公開するHID Report Descriptor
  （`src/internal/EspBleBluedroidHidReportMaps.h`）と、1つのReport Map characteristicへの
  合成規則を扱う。バイト列はEspBleの表と比較する（`espble.hid_maps`、
  `tools/gen_hid_report_maps.py`で再生成）。host OSがこのバイト列を解析するため。さらに
  合成後のmapを共有parserへ通し、EspBleと同一でも意味が誤っている場合を捕まえる。profileの
  種別とReport ID、LED Output Report、NKRO bitmap、report長を変えてはならないマウスの
  ボタン数、vendor report size。簡易keyboard検出が6KRO（boot protocol用）のみを認識し、
  NKROには完全なparserが必要であることも固定する。
- `api_parity`: `src/EspBleBluedroid.h`の公開API面をEspBleのsnapshot
  （`espble.symbols`）と突き合わせ、`docs/API_PARITY.tsv`に理由付きで分類されていない
  差分があれば失敗する。さらに`*Name()`関数の**戻り値**——`src/EspBleBluedroid.cpp`の
  enum→文字列対応を`espble.values`と比較——も検証する。signatureが完全に一致していても
  利用側へ渡す文字列が食い違う余地があり、それはheaderには現れないため。committed snapshot
  2つを読むだけなので、EspBleのcheckoutは不要。
  詳細は[../TEST_PLAN.ja.md](../TEST_PLAN.ja.md#espbleとのapi整合をテストで固定する)。

`keymap`、`report_map`、`midi`が対象とするheaderはEspBleからのverbatim copyであるため、
テストプログラムもEspBleと同じものを使用しています。これらはBLE MIDI profile
（`src/EspBleMidiProfile.h`、実装済み）と、未実装のHID over GATTの土台です。
