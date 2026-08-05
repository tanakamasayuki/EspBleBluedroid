// en: KeyboardHost - connect to a BLE keyboard as a HID host: scan the HID service ->
//     pair -> discover -> print key events. Works with commercial BLE keyboards and with
//     the KeyboardDevice example. Also writes the keyboard LEDs.
// ja: KeyboardHost - HID HostとしてBLE keyboardへ接続する。HID Serviceをscan → Pairing →
//     Discovery → キーイベント表示。市販BLE keyboardでも KeyboardDevice example でも使える。
//     keyboard LEDの書込みも行う。
#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;
EspBleConnectionId keyboardConnectionId = 0; // en: currently connected keyboard / ja: 現在接続中のkeyboard

void setup()
{
  Serial.begin(115200);

  auto &keyboard = bluetooth.hidHost();
  // en: Discovery-complete (now available) notification, including report ID and battery.
  // ja: Discovery完了（利用可能になった）通知。Report IDやBattery情報を含む。
  keyboard.onDiscovered([](const EspBleHidKeyboardHostDiscovery &result) {
    if (!result.success)
    {
      // en: Read result.detail here, not lastErrorName(): this callback runs after the
      //     discovery sequence finished, so the library's last error has moved on.
      // ja: ここでは lastErrorName() ではなく result.detail を読む。このcallbackは
      //     Discovery列の完了後に走るため、ライブラリのlastErrorは既に別の値になっている。
      Serial.printf("Keyboard discovery failed: %s\n", result.detail.c_str());
      return;
    }
    Serial.printf(
      "Keyboard ready: report=%u battery=%s%u%%\n",
      result.reportId,
      result.hasBatteryLevel ? "" : "unknown/",
      result.batteryLevel);
  });
  // en: Layout-independent 256-bit usage snapshot. Query via isDown/wasPressed/wasReleased.
  // ja: layout非依存の256-bit usage snapshot。isDown/wasPressed/wasReleasedで照会。
  keyboard.onKeyboardState([](const EspBleHidKeyboardState &state) {
    Serial.printf(
      "Keyboard state: modifiers=0x%02x A=%u pressed=%u released=%u\n",
      state.modifiers,
      state.isDown(0x04) ? 1 : 0,      // en: usage 0x04 = A / ja: usage 0x04 = A
      state.wasPressed(0x04) ? 1 : 0,
      state.wasReleased(0x04) ? 1 : 0);
  });
  // en: Select the layout used for conversion (reflected in onKeyboard's ascii).
  // ja: 変換用layoutを選ぶ（onKeyboardのasciiに反映）。
  keyboard.setKeyboardLayout(EspBleKeyboardLayout::EnUs);
  // en: Per-usage press/release convenience event; ascii is nonzero only when convertible.
  //     A modifier arrives as an event of its own (usage 0xe0-0xe7, ascii 0) as well as
  //     in event.modifiers.
  // ja: usage単位のpress/release convenience event。asciiは変換可能な場合のみ非0。
  //     modifierは event.modifiers に載るのとは別に、単独のevent（usage 0xe0-0xe7、
  //     ascii 0）としても届く。
  keyboard.onKeyboard([](const EspBleHidKeyboardEvent &event) {
    if (event.pressed)
    {
      Serial.printf("Key pressed: usage=0x%02x ascii=0x%02x\n", event.usage, event.ascii);
    }
  });
  // en: The same Host object dispatches every supported composite-HID report.
  // ja: 同じHost objectが複合HIDの全対応Reportを配送する。
  keyboard.onMouse([](const EspBleHidMouseEvent &event) {
    Serial.printf("Mouse: x=%d y=%d wheel=%d buttons=0x%02x\n",
      event.x, event.y, event.wheel, event.buttons);
  });
  keyboard.onConsumerControl([](const EspBleHidConsumerControlEvent &event) {
    Serial.printf("Consumer: usage=0x%04x pressed=%u\n", event.usage, event.pressed ? 1 : 0);
  });
  keyboard.onSystemControl([](const EspBleHidSystemControlEvent &event) {
    Serial.printf("System: usage=0x%02x pressed=%u\n", event.usage, event.pressed ? 1 : 0);
  });
  keyboard.onGamepad([](const EspBleHidGamepadEvent &event) {
    Serial.printf("Gamepad: fields=%u changed=%u\n",
      static_cast<unsigned>(event.fieldCount), event.changed ? 1 : 0);
  });

  EspBleConfig config;
  config.deviceName = "Bluedroid Keyboard Host";
  // en: Commercial keyboards require encryption on their HID attributes, so security is needed.
  // ja: 市販keyboardはHID属性に暗号化を要求するため、security有効化が必要。
  config.security.enabled = true;
  config.security.bonding = true;
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    keyboardConnectionId = connection.id;
  });
  // en: In a security-enabled setup, start HID discovery after encryption completes
  //     (commercial keyboards reject HID-attribute access before encryption).
  // ja: security有効構成では、暗号化完了後にHID Discoveryを開始する
  //     （市販keyboardは暗号化前のHID属性アクセスを拒否するため）。
  // en: For hands-off reconnection, call bluetooth.hidHost().setAutoRediscover(true)
  //     once: a HID peer this host discovered before is re-discovered automatically
  //     after it reconnects and re-encrypts. Reconnecting itself is this sketch's job
  //     here — there is no setAutoReconnect() in this library (see the README).
  // ja: 再接続を自動化するなら bluetooth.hidHost().setAutoRediscover(true) を一度呼ぶ。
  //     一度Discoveryしたpeerは、再接続・再暗号化後に自動で再Discoveryされる。ただし
  //     再接続そのものはこのsketchの仕事で、本ライブラリに setAutoReconnect() は無い
  //     （READMEを参照）。
  bluetooth.onSecurityChanged([](const EspBleSecurityChanged &event) {
    if (event.success)
    {
      bluetooth.hidHost().discover(event.connection.id);
    }
  });
  bluetooth.onDisconnected([](const EspBleConnection &) {
    keyboardConnectionId = 0;
  });
  // en: Connect to a connectable device advertising HID Service 0x1812.
  // ja: HID Service 0x1812 をadvertiseするconnectableな機器へ接続する。
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (result.connectable && result.advertisesService("1812"))
    {
      bluetooth.scanner().stop();
      bluetooth.connect(result);
    }
  });
  bluetooth.scanner().start();
}

void loop()
{
  // en: Accept LED commands only while connected.
  // ja: 接続中のみLEDコマンドを受け付ける。
  if (Serial.available() > 0 && keyboardConnectionId != 0)
  {
    const char command = Serial.read();
    if (command == 'c')
    {
      // en: turn on the Caps Lock LED (fire-and-forget Write Without Response)
      // ja: Caps Lock LED点灯（Write Without Responseのfire-and-forget）
      bluetooth.hidHost().setKeyboardLeds(
        keyboardConnectionId, false, true, false);
    }
    else if (command == '0')
    {
      // en: turn off all LEDs / ja: 全LED消灯
      bluetooth.hidHost().setKeyboardLeds(
        keyboardConnectionId, false, false, false);
    }
  }

  // en: Discovery/state/key events are delivered from this update().
  // ja: Discovery/state/keyイベントはこの update() から配送される。
  bluetooth.update();
  delay(1);
}
