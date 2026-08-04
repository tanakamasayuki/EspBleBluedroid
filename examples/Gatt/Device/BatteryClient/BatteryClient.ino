#include <EspBleBluedroid.h>

static constexpr const char *BATTERY_SERVICE_UUID = "180f";
static constexpr const char *BATTERY_LEVEL_UUID = "2a19";

EspBleBluedroid bluetooth;
bool connectionRequested = false;

void setup()
{
  Serial.begin(115200);

  if (!bluetooth.begin())
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    bluetooth.readCharacteristic(connection.id, BATTERY_SERVICE_UUID, BATTERY_LEVEL_UUID);
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    if (!result.success || result.value.length() != 1)
    {
      Serial.printf("Battery read failed: %s\n", result.detail.c_str());
      return;
    }
    Serial.printf("Battery: %u%%\n", static_cast<uint8_t>(result.value[0]));
    bluetooth.subscribe(result.connectionId, BATTERY_SERVICE_UUID, BATTERY_LEVEL_UUID);
  });
  bluetooth.onSubscribed([](const EspBleGattResult &result) {
    Serial.printf("Battery subscription: %s\n", result.success ? "ready" : "failed");
  });
  bluetooth.onNotification([](const EspBleGattNotification &notification) {
    if (notification.serviceUuid.equalsIgnoreCase(BATTERY_SERVICE_UUID) &&
        notification.characteristicUuid.equalsIgnoreCase(BATTERY_LEVEL_UUID) &&
        notification.value.length() == 1)
    {
      Serial.printf("Battery changed: %u%%\n",
        static_cast<uint8_t>(notification.value[0]));
    }
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested || !result.advertisesService(BATTERY_SERVICE_UUID)) return;
    bluetooth.scanner().stop();
    connectionRequested = bluetooth.connect(result);
  });

  EspBleScanConfig scan;
  scan.active = true;
  bluetooth.scanner().start(scan);
}

void loop()
{
  bluetooth.update();
  delay(1);
}
