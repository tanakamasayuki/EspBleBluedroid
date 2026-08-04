// en: SubscribeClient - connect to the Gatt/Basics/NotifyServer example, subscribe to
//     notifications, and print each received value.
// ja: SubscribeClient - Gatt/Basics/NotifyServer example へ接続し、Notificationを購読して
//     受信値を表示する。
#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID = "71756360-5fa4-43bc-9003-6e6f74696679";
static constexpr const char *CHARACTERISTIC_UUID = "71756361-5fa4-43bc-9003-6e6f74696679";

EspBleBluedroid bluetooth;
bool connectionRequested = false;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "Bluedroid Subscribe Client";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  // en: Subscribe after connecting (4th arg true = notifications, false = indications).
  // ja: 接続完了後にNotificationを購読する（第4引数true = Notification、false = Indication）。
  bluetooth.onConnected([](const EspBleConnection &connection) {
    if (!bluetooth.subscribe(connection.id, SERVICE_UUID, CHARACTERISTIC_UUID, true))
    {
      Serial.printf("Subscribe request failed: %s\n", bluetooth.lastErrorDetail().c_str());
    }
  });
  // en: Subscription (CCCD write) completion.
  // ja: 購読（CCCD書込み）の完了。
  bluetooth.onSubscribed([](const EspBleGattResult &result) {
    if (!result.success)
    {
      Serial.printf("Subscribe failed: %s\n", result.detail.c_str());
    }
  });
  // en: A received notification (payload is copied).
  // ja: 受信したNotification（payloadはcopy済み）。
  bluetooth.onNotification([](const EspBleGattNotification &notification) {
    Serial.printf("Notification: %s\n", notification.value.c_str());
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &scanResult) {
    if (connectionRequested || !scanResult.advertisesService(SERVICE_UUID))
    {
      return;
    }
    bluetooth.scanner().stop();
    connectionRequested = bluetooth.connect(scanResult);
  });

  EspBleScanConfig scanConfig;
  scanConfig.active = true;
  bluetooth.scanner().start(scanConfig);
}

void loop()
{
  // en: Subscription and notification events are delivered from this update().
  // ja: 購読完了・Notificationイベントはこの update() から配送される。
  bluetooth.update();
  delay(1);
}
