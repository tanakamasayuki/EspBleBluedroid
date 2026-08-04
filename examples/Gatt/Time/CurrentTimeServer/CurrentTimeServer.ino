#include <EspBleBluedroid.h>

static constexpr const char *CURRENT_TIME_SERVICE_UUID = "1805";
static constexpr const char *CURRENT_TIME_UUID = "2a2b";

EspBleBluedroid bluetooth;
EspBleGattService currentTimeServiceService;
EspBleGattCharacteristic currentTimeCharacteristic;
// 2026-07-19 12:34:56, Sunday, 0/256 second, manually adjusted.
uint8_t currentTime[] = {0xea, 0x07, 7, 19, 12, 34, 56, 7, 0, 0x01};

static void publishCurrentTime()
{
  auto &server = bluetooth.gattServer();
  server.setValue(currentTimeCharacteristic, currentTime, sizeof(currentTime));
  const bool notified = server.notify(currentTimeCharacteristic, currentTime, sizeof(currentTime));
  Serial.printf("Time: %04u-%02u-%02u %02u:%02u:%02u (notification accepted: %u)\n",
    static_cast<unsigned>(currentTime[0] | (currentTime[1] << 8)),
    currentTime[2], currentTime[3], currentTime[4], currentTime[5], currentTime[6],
    notified ? 1 : 0);
}

void setup()
{
  Serial.begin(115200);

  EspBleGattCharacteristicConfig timeConfig;
  timeConfig.readable = true;
  timeConfig.notifiable = true;
  auto &server = bluetooth.gattServer();
  if (!(currentTimeServiceService = server.addService(CURRENT_TIME_SERVICE_UUID)).valid() ||
      !(currentTimeCharacteristic = server.addCharacteristic(currentTimeServiceService, CURRENT_TIME_UUID, timeConfig)).valid() ||
      !server.setValue(currentTimeCharacteristic, currentTime, sizeof(currentTime)))
  {
    Serial.printf("Current Time configuration failed: %s\n",
      bluetooth.lastErrorDetail().c_str());
    return;
  }

  EspBleConfig config;
  config.deviceName = "Bluedroid Current Time";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }
  bluetooth.advertising().setName("Bluedroid Current Time");
  bluetooth.advertising().addServiceUuid(CURRENT_TIME_SERVICE_UUID);
  bluetooth.advertising().start();
  Serial.println("Send 't' to advance one second and notify subscribers.");
}

void loop()
{
  if (Serial.available() > 0 && Serial.read() == 't')
  {
    currentTime[6] = static_cast<uint8_t>((currentTime[6] + 1) % 60);
    publishCurrentTime();
  }
  bluetooth.update();
  delay(1);
}
