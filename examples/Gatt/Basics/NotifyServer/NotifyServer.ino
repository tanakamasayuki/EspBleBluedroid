#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID =
  "71756360-5fa4-43bc-9003-6e6f74696679";
static constexpr const char *CHARACTERISTIC_UUID =
  "71756361-5fa4-43bc-9003-6e6f74696679";

EspBleBluedroid bluetooth;
EspBleGattCharacteristic counterCharacteristic;
bool subscribed = false;
uint32_t lastSent = 0;
uint32_t counter = 0;

void setup()
{
  Serial.begin(115200);
  auto &server = bluetooth.gattServer();
  EspBleGattCharacteristicConfig config;
  config.readable = true;
  config.notifiable = true;
  const EspBleGattService service = server.addService(SERVICE_UUID);
  counterCharacteristic = server.addCharacteristic(
    service, CHARACTERISTIC_UUID, config);
  server.onSubscriptionChanged([](const EspBleGattSubscription &event) {
    if (event.characteristic == counterCharacteristic)
      subscribed = event.notifications;
  });
  EspBleConfig bluetoothConfig;
  bluetoothConfig.deviceName = "EspBleBluedroid Notify Server";
  if (!bluetooth.begin(bluetoothConfig)) return;
  bluetooth.advertising().setName(bluetoothConfig.deviceName);
  bluetooth.advertising().addServiceUuid(SERVICE_UUID);
  bluetooth.advertising().start();
}

void loop()
{
  bluetooth.update();
  if (subscribed && millis() - lastSent >= 1000)
  {
    lastSent = millis();
    bluetooth.gattServer().notify(
      counterCharacteristic, String(++counter));
  }
  delay(1);
}
