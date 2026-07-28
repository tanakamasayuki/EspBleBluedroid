# BLE直接バックエンド移行計画

## 目的

EspBleBluedroidのBLE実装からArduino-ESP32の`BLEDevice`、`BLEClient`、
`BLEScan`、`BLEAdvertising`、`BLESecurity`、`BLERemote*`などへの依存を
取り除き、Arduino-ESP32 3.3.11が公開するESP-IDF Bluedroid APIへ直接接続する。

公開APIはbackend objectを露出していないため、利用者側の使い方は原則維持する。
移行の目的は機能追加ではなく、次をlibrary自身の責任として明確にすることである。

- stack lifecycleとGAP / GATT callbackの所有権
- 非同期操作の状態、timeout、cancel、完了条件
- Advertising DataとScan Resultのwire codec
- connection、GATT database、Securityの値型snapshot
- Arduino-ESP32 BLE wrapper更新からの影響範囲の縮小

Bluetooth ClassicとSPPはすでにESP-IDF APIへ直接接続しているため、この移行の対象外とする。
ただしBLEとClassicで共有するcontroller / Bluedroid hostの開始・終了順は最終段階で
まとめて再検証する。

## 完了条件

移行完了は、単に通常動作が通ることではなく、次をすべて満たした状態とする。

1. `src/`からArduino BLE libraryのheaderと型参照がなくなる。
2. `BLEDevice::init()` / `deinit()`を使わず、rootがcontrollerとBluedroid hostを所有する。
3. GAP、GATTC、将来のGATTS callbackをlibraryのevent routerへ直接登録する。
4. Scan、Advertising、接続、GATT Client、BLE SecurityがESP-IDF公開APIだけで動作する。
5. 接続・GATT操作がstack event駆動になり、wrapperの同期API用worker taskを撤去する。
6. 公開callbackは従来どおり`update()` contextから配送する。
7. `end()`が未完了のscan、advertising、接続、GATT、Security操作をcancelし、
   古いeventを次のlifecycleへ残さない。
8. generic `esp32`、Arduino-ESP32 3.3.11、PSRAMなしを基準に全exampleをbuildできる。
9. 既存peerテストがすべて通り、各移行段階で追加した失敗・timeout・再初期化試験も通る。

`library.properties`へArduino BLE library依存は追加しない。Arduino coreの
`String`、`Stream`、Bluetooth controller起動用の公開Arduino HALは引き続き利用してよい。

## 移行原則

- 公開動作を変えるsliceは、失敗するunitまたはpeerテストを先に追加する。
- Arduino BLE wrapperと直接backendの恒久的な二重実装は持たない。
- wrapperのprivate member、object layout、非公開symbolは利用しない。
- 各sliceは接続可能またはbuild可能な状態で完了させ、長期間の全機能停止を作らない。
- callback内では必要な値だけを固定上限のlibrary stateへcopyし、利用者コードを呼ばない。
- ESP-IDF由来のhandleとpointerは公開APIへ露出しない。
- resource上限、queue overflow、timeout、cancel失敗は観測可能にする。

## 段階

### 1. backend非依存codec

対象:

- BLE addressのparse / format
- 16 / 32 / 128-bit UUIDのparse、比較、Advertising用little-endian変換
- Legacy Advertising Data fieldの長さ検証と連結
- Scan Resultで必要なAdvertising Data field parser

先に`tests/unit`で正常値、大文字小文字、Bluetooth Base UUIDとの等価性、不正入力、
31 byte境界、壊れたfield lengthを固定する。その後、`BLEAddress`、`BLEUUID`、
`BLEAdvertisementData`の純粋処理から順に内部codecへ置き換える。

この段階では接続・Scan・Advertisingのbackend objectは残してよい。wire dataと公開値が
変わらないことを既存Advertising / Scan peerテストで確認する。

### 2. GATTC接続とGATT Client

1つのGATTC applicationをroot lifecycleごとに登録し、`gattc_if`、`conn_id`、
peer address、操作generationをlibrary stateで保持する。

- `esp_ble_gattc_app_register()`完了後に接続要求を受理可能にする。
- `esp_ble_gattc_open()`の完了を`OPEN` / `CONNECT` eventから判定する。
- 接続timeout中は`esp_ble_gattc_cancel_open()`を使い、成立済みなら
  `esp_ble_gap_disconnect()`または`esp_ble_gattc_close()`を使う。
- Service Search、Characteristic / Descriptor列挙、Read / Write、Notify登録を
  GATTC eventだけで進める。
- CCCD writeとnotification登録を別の完了条件として管理する。
- late eventはconnection IDではなく内部generationとbackend identityで排除する。

既存の公開上限（Service 16、Characteristic 48、Descriptor 48、同時GATT操作1件）は
維持する。同期wrapperを呼ぶ専用worker taskとsemaphoreは、この段階の完了時に削除する。

必須peer試験は接続成功、厳密な接続timeout、接続途中の`end()`、切断と再接続、
Discovery、全Read / Write、Subscribe / Unsubscribe、ATT失敗、操作timeout、
操作中切断、late event抑止とする。

### 3. Scan

`esp_ble_gap_set_scan_params()`、`esp_ble_gap_start_scanning()`、
`esp_ble_gap_stop_scanning()`と`ESP_GAP_BLE_SCAN_RESULT_EVT`へ直接接続する。

raw Advertising / Scan Responseを段階1のparserへ渡し、既存のaddress単位merge、
duplicate方針、16件queue、overflow計数を維持する。own address typeとCentral側
Filter Accept Listをwrapper制約なしで指定できる内部境界を用意する。

### 4. Legacy Advertising

段階1のcodecでAdvertising DataとScan Responseの最大31 byte payloadを構築し、
raw Advertising設定APIと完了eventを使う。設定完了前にstartせず、stop、時間停止、
再設定、Directed Advertisingとの排他を単一の状態機械で管理する。

Directed Advertising、Accept List、address / privacy、Tx Powerはすでに直接APIを
利用しているため、通常Advertisingを同じevent routerへ統合する。

### 5. BLE Security

I/O capability、authentication mode、key distribution、key size、静的passkeyを
`esp_ble_gap_set_security_param()`で設定し、暗号化開始は`esp_ble_set_encryption()`を使う。
Passkey Entry、Numeric Comparison、Security Request、Authentication Completeを
GAP event routerで処理する。

静的passkeyを使わない構成へ変更するときはclear parameterを明示的に設定し、
現在のwrapper由来の「同一bootで構成を解除できない」という制限を削除する。

### 6. lifecycleとcallback所有権

GAP / GATTCでwrapper objectを使わなくなった後、最後に`BLEDevice`を撤去する。

- Arduino HALでBluetooth controllerを必要なmodeで開始する。
- Bluedroid hostをinit / enableする。
- GAP / GATTC / GATTS callbackを直接登録する。
- device name、local MTU、Security既定値を設定する。
- 終了はprofile、link、GATT application、host、controllerの逆順に行う。

Arduino coreがBLE未使用と判断して起動時にBLEメモリを解放しないよう、Arduino-ESP32
3.3.11の公開HAL headerを明示的に利用する。Classicを含むdual modeでcontrollerを
二重初期化しないことをpeerテストする。

### 7. 依存撤去と回帰

Arduino BLE headerと型参照が0件であることをCIで検査する。全unit test、全peer test、
全example buildを実行し、heap使用量とtask数が移行前から不必要に増えていないことを
確認する。

## 段階移行中のevent配送

Arduino-ESP32 3.3.11の`BLEDevice`は、wrapper内の配送後にcustom GAP / GATTC handlerを
呼ぶ。したがって段階2〜5では`BLEDevice`をlifecycleとroot callback登録だけに残し、
移行済み機能ではwrapper objectを生成せず、libraryのcustom handlerで直接eventを処理する。

GATTC、Scan、Advertising、Securityの順にwrapper objectをなくした後、段階6でroot callback
登録をlibrary自身へ切り替える。この順序により、private dispatcherを呼び出す中間adapterや
全機能を一度に置換する大規模変更を避ける。

## 検証コマンド

unit testとpeer testは`tests/`を作業directoryとして実行する。

```sh
cd tests
uv run --env-file .env pytest unit
uv run --env-file .env pytest peer/connect_disconnect
uv run --env-file .env pytest peer/gatt_client
```

全peer試験では`.env`のポートを使用し、`/dev/ttyUSB0`や`/dev/ttyUSB1`へ固定しない。
通常のbuild確認はgeneric `esp32`だけを対象とし、同じESP32内のboard matrixは作らない。
