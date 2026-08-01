# Classic ProfileSupport

Bluetooth stackを初期化せずに、主要なClassic profileの対応状態と理由を表示します。

状態は次を区別します。

- `Supported`: CoreとEspBleBluedroidの両方で利用可能
- `LibraryNotImplemented`: Coreは利用可能だがライブラリAPIが未実装
- `CoreDisabled`: Coreのbuild optionで無効
- `CoreApiUnavailable`: Coreに利用可能なpublic profile APIがない
- `NoStandardProfile`: 標準Classic profile自体が存在しない

GamePadは`HidDevice`または`HidHost`を確認します。Arduino-ESP32 3.3.11の標準buildでは
`CONFIG_BT_HID_ENABLED`が無効なので`CoreDisabled`になります。

