#include <EspBleBluedroid.h>

static constexpr const char *BATTERY_SERVICE_UUID = "180f";
static constexpr const char *BATTERY_LEVEL_UUID = "2a19";

EspBleBluedroid bluetooth;
EspBleGattService batteryServiceService;
EspBleGattCharacteristic batteryLevelCharacteristic;
uint8_t batteryLevel = 75;

static void publishBatteryLevel()
{
  auto &server = bluetooth.gattServer();
  server.setValue(batteryLevelCharacteristic, &batteryLevel, 1);
  const bool notified = server.notify(batteryLevelCharacteristic, &batteryLevel, 1);
  Serial.printf("Battery: %u%% (notification accepted: %u)\n",
    batteryLevel, notified ? 1 : 0);
}

void setup()
{
  Serial.begin(115200);

  EspBleGattCharacteristicConfig levelConfig;
  levelConfig.readable = true;
  levelConfig.notifiable = true;

  auto &server = bluetooth.gattServer();
  if (!(batteryServiceService = server.addService(BATTERY_SERVICE_UUID)).valid() ||
      !(batteryLevelCharacteristic = server.addCharacteristic(batteryServiceService, BATTERY_LEVEL_UUID, levelConfig)).valid() ||
      !server.setValue(batteryLevelCharacteristic, &batteryLevel, 1))
  {
    Serial.printf("Battery configuration failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }
  server.onSubscriptionChanged([](const EspBleGattSubscription &subscription) {
    Serial.printf("Battery notifications: %u\n", subscription.notifications ? 1 : 0);
  });

  EspBleConfig config;
  config.deviceName = "Bluedroid Battery";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.advertising().setName("Bluedroid Battery");
  bluetooth.advertising().addServiceUuid(BATTERY_SERVICE_UUID);
  bluetooth.advertising().start();
  Serial.println("Send '+' or '-' to change the Battery Level.");
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == '+' && batteryLevel < 100)
    {
      ++batteryLevel;
      publishBatteryLevel();
    }
    else if (command == '-' && batteryLevel > 0)
    {
      --batteryLevel;
      publishBatteryLevel();
    }
  }
  bluetooth.update();
  delay(1);
}
