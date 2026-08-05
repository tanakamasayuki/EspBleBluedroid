// en: Mouse - a BLE HID mouse over GATT (HOGP): a relative-motion pointer with
//     buttons and a wheel. Motion and clicks are triggered by Serial commands.
// ja: Mouse - BLE HID mouse（HID over GATT / HOGP）。相対移動のポインタ＋ボタン＋ホイール。
//     移動やクリックはSerialコマンドで発生させる。
#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);

  // en: Configure the profile before begin(). A mouse-only device composes the same
  //     HID + Battery + Device Information services a keyboard does; the Report Map
  //     holds just the mouse descriptor.
  // ja: begin() 前にprofileを構成する。mouse単体でもkeyboardと同じHID + Battery +
  //     Device Information Serviceが合成され、Report Mapにはmouseのdescriptorだけが入る。
  EspBleHidMouseConfig mouseConfig;
  // en: Three buttons instead of the default five: the count is patched into the
  //     Report Descriptor, and the report stays 4 bytes whatever it is.
  // ja: 既定の5個ではなく3個にする。個数はReport Descriptorへ埋め込まれ、
  //     Reportはいくつでも4byteのまま。
  mouseConfig.buttons = 3;
  bluetooth.hidMouse().configure(mouseConfig);

  EspBleConfig config;
  config.deviceName = "Bluedroid Mouse";
  // en: HOGP requires an encrypted link, so enabling security is effectively required.
  // ja: HOGPは暗号化linkを要求するため、security有効化が実質必須。
  config.security.enabled = true;
  config.security.bonding = true;
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onDisconnected([](const EspBleConnection &) {
    bluetooth.advertising().start();
  });
  bluetooth.advertising().setName(config.deviceName);
  bluetooth.advertising().start();

  Serial.println("Send 'm' to move, 'c' to click, 'w' to scroll, 'd' to drag.");
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    auto &mouse = bluetooth.hidMouse();
    if (command == 'm') mouse.move(12, -8);
    else if (command == 'c') mouse.click(ESP_BLE_HID_MOUSE_LEFT);
    else if (command == 'w') mouse.wheel(1);
    else if (command == 'd')
    {
      // en: A drag is a move with the button still down: move() keeps whatever
      //     buttons() reports as held, so no button state has to be repeated.
      // ja: ドラッグは「ボタンを押したままの移動」。move() は buttons() が保持している
      //     ボタンをそのまま維持するので、ボタン状態を毎回指定する必要はない。
      mouse.press(ESP_BLE_HID_MOUSE_LEFT);
      mouse.move(20, 0);
      mouse.releaseAll();
    }
  }

  bluetooth.update();
  delay(1);
}
