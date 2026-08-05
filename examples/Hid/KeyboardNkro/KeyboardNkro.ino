// en: KeyboardNkro - n-key rollover: the whole keyboard state travels as one report
//     (a modifier byte plus a 224-bit usage bitmap), so any number of keys can be
//     held at once instead of the six a boot-compatible report carries.
// ja: KeyboardNkro - n-key rollover。キーボード全体の状態を1 Reportで送る（modifier 1byte
//     ＋224bitのusage bitmap）ので、boot互換Reportの6キー制限なく同時押しできる。
#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);

  auto &keyboard = bluetooth.hidKeyboard();
  // en: enableNkro() must come before configure(): it selects which keyboard
  //     descriptor goes into the Report Map, which is composed there.
  // ja: enableNkro() は configure() より前に呼ぶ。どのkeyboard descriptorをReport Mapへ
  //     入れるかを決め、Report Mapは configure() で合成されるため。
  keyboard.enableNkro();
  keyboard.configure();

  EspBleConfig config;
  config.deviceName = "Bluedroid NKRO Keyboard";
  // en: A 29-byte NKRO Input Report needs an ATT payload of 29, i.e. MTU >= 32.
  // ja: 29byteのNKRO Input Reportには29byteのATT payload、つまりMTU >= 32が必要。
  config.preferredMtu = 64;
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

  Serial.println("Send 'n' for eight simultaneous keys, 'r' to release all.");
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    auto &keyboard = bluetooth.hidKeyboard();
    // en: ready() is false until a host is connected, encrypted, and subscribed to
    //     the Input Report. A host that has not arrived yet is a normal state.
    // ja: ready() はHostが接続し暗号化しInput Reportをsubscribeするまでfalse。
    //     Hostがまだ来ていないのは異常ではなく通常の状態。
    if (!keyboard.ready())
    {
      Serial.println("No subscribed HID Host yet.");
    }
    else if (command == 'n')
    {
      // en: sendReport(EspBleHidKeyboardReport) carries keys[6] and still expresses
      //     only six usages with NKRO enabled, so eight keys go out as one
      //     whole-state report. pressUsage() could hold eight too, but each key
      //     change would be its own notification, paced by the connection interval.
      // ja: sendReport(EspBleHidKeyboardReport) は keys[6] しか持たずNKRO有効でも6 usage
      //     までなので、8キーは状態全体の1 Reportで送る。pressUsage() でも8キー保持できるが、
      //     キー変化ごとに別notificationになり接続間隔に律速される。
      EspBleHidKeyboardNkroReport report;
      const uint8_t usages[] = {0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b};
      for (uint8_t usage : usages) report.press(usage);
      keyboard.sendReport(report);
    }
    else if (command == 'r')
    {
      keyboard.releaseAll();
    }
  }

  bluetooth.update();
  delay(1);
}
