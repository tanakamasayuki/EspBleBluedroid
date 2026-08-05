// en: ConsumerControl - the media keys of a keyboard as their own HID device: one
//     16-bit Consumer page usage per report (volume, play/pause, track skip).
// ja: ConsumerControl - キーボードのメディアキーを単体のHIDデバイスにする。1 Reportに
//     Consumer pageのusageを1つ（16bit）だけ載せる（音量・再生/一時停止・曲送り）。
#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);

  bluetooth.hidConsumerControl().configure();

  EspBleConfig config;
  config.deviceName = "Bluedroid Media Keys";
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

  Serial.println("Send '+', '-', or 'p'.");
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    auto &media = bluetooth.hidConsumerControl();
    // en: click() presses and releases, because a media key is a momentary action:
    //     the release report (usage 0) is what stops the host from repeating it.
    // ja: メディアキーは瞬間的な操作なので click() で押して離す。release Report
    //     （usage 0）を送ることでHost側の連続実行が止まる。
    if (command == '+') media.click(ESP_BLE_HID_CONSUMER_CONTROL_VOLUME_UP);
    else if (command == '-') media.click(ESP_BLE_HID_CONSUMER_CONTROL_VOLUME_DOWN);
    else if (command == 'p') media.click(ESP_BLE_HID_CONSUMER_CONTROL_PLAY_PAUSE);
  }

  bluetooth.update();
  delay(1);
}
