// en: PhoneAlertStatusClient - connect to a Phone Alert Status Service (0x180E),
//     read Alert Status, subscribe to Ringer Setting, and drive the Ringer
//     Control Point (Write Without Response) to set Silent Mode then Cancel it,
//     printing the notified Ringer Setting each time.
// ja: PhoneAlertStatusClient - Phone Alert Status Service（0x180E）へ接続し、
//     Alert StatusをRead、Ringer Settingを購読、Ringer Control Point（Write
//     Without Response）でSilent Mode設定→解除を行い、NotifyされたRinger Setting
//     を毎回表示する。
#include <EspBleBluedroid.h>

static constexpr const char *PASS_SERVICE_UUID = "180e";
static constexpr const char *ALERT_STATUS_UUID = "2a3f";
static constexpr const char *RINGER_SETTING_UUID = "2a41";
static constexpr const char *RINGER_CONTROL_POINT_UUID = "2a40";

EspBleBluedroid bluetooth;
EspBleConnectionId connectionId = 0;
unsigned long lastToggle = 0;
uint8_t nextCommand = 1; // en: 1 = Silent, 3 = Cancel / ja: 1 = Silent、3 = Cancel

static void writeRingerControl(uint8_t command)
{
  // en: Ringer Control Point is Write Without Response.
  // ja: Ringer Control PointはWrite Without Response。
  bluetooth.writeCharacteristic(
    connectionId, PASS_SERVICE_UUID, RINGER_CONTROL_POINT_UUID, &command, sizeof(command), false);
}

void setup()
{
  Serial.begin(115200);
  if (!bluetooth.begin())
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    bluetooth.readCharacteristic(connection.id, PASS_SERVICE_UUID, ALERT_STATUS_UUID);
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    connectionId = 0;
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    if (result.characteristicUuid.equalsIgnoreCase(ALERT_STATUS_UUID) &&
        result.success && result.value.length() == 1)
    {
      Serial.printf("Alert Status: 0x%02x\n", static_cast<uint8_t>(result.value[0]));
      bluetooth.subscribe(result.connectionId, PASS_SERVICE_UUID, RINGER_SETTING_UUID);
    }
  });
  bluetooth.onNotification([](const EspBleGattNotification &notification) {
    if (!notification.characteristicUuid.equalsIgnoreCase(RINGER_SETTING_UUID) ||
        notification.value.length() != 1)
      return;
    const uint8_t setting = static_cast<uint8_t>(notification.value[0]);
    Serial.printf("Ringer Setting: %s\n", setting == 0 ? "Silent" : "Normal");
  });

  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (result.connectable && result.advertisesService(PASS_SERVICE_UUID))
    {
      bluetooth.scanner().stop();
      bluetooth.connect(result);
    }
  });
  bluetooth.scanner().start();
}

void loop()
{
  // en: Every 5 seconds, toggle between Set Silent Mode and Cancel Silent Mode.
  // ja: 5秒ごとに、Set Silent ModeとCancel Silent Modeを切り替える。
  if (connectionId != 0 && millis() - lastToggle >= 5000)
  {
    lastToggle = millis();
    writeRingerControl(nextCommand);
    nextCommand = (nextCommand == 1) ? 3 : 1;
  }

  bluetooth.update();
  delay(1);
}
