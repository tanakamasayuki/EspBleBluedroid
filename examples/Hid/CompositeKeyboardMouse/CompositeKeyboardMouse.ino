// en: CompositeKeyboardMouse - one device that is both a keyboard and a mouse. HOGP
//     gives a device a single HID service, so both profiles share it and their
//     reports are told apart by Report ID.
// ja: CompositeKeyboardMouse - keyboardとmouseを兼ねる1台のデバイス。HOGPではHID Serviceは
//     1つなので両profileがそれを共有し、ReportはReport IDで区別される。
#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);

  // en: Configure every profile before begin(): the Report Map is composed once,
  //     from whatever has been registered by then.
  // ja: すべてのprofileを begin() 前に構成する。Report Mapはその時点で登録済みのものから
  //     1度だけ合成される。
  bluetooth.hidKeyboard().configure();
  bluetooth.hidMouse().configure();

  EspBleConfig config;
  config.deviceName = "Bluedroid Composite HID";
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

  Serial.println("Send 'h' to type hello, 'm' to move the pointer.");
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    // en: Each profile subscribes separately, so ready() is a per-profile question:
    //     a host may be listening to the keyboard and not to the mouse.
    // ja: subscribeはprofileごとなので ready() もprofileごとの問い。Hostがkeyboardだけ
    //     購読していてmouseは購読していない、という状態があり得る。
    if (command == 'h') bluetooth.hidKeyboard().write("hello");
    else if (command == 'm') bluetooth.hidMouse().move(10, 10);
    else if (command == '?')
    {
      Serial.printf("ready: keyboard=%u mouse=%u\n",
        bluetooth.hidKeyboard().ready() ? 1 : 0,
        bluetooth.hidMouse().ready() ? 1 : 0);
    }
  }

  bluetooth.update();
  delay(1);
}
