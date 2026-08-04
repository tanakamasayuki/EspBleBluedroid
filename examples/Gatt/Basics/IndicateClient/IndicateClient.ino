// en: IndicateClient - indication counterpart of SubscribeClient: subscribes with
//     notifications=false, so the stack acknowledges every received value and the
//     server's onSent() reports the confirmed delivery. Pair with Gatt/Basics/IndicateServer.
// ja: IndicateClient - SubscribeClientのIndication版。notifications=false で購読するため、
//     受信した各値をスタックが確認応答し、Server側の onSent() が配信確認を報告する。
//     Gatt/Basics/IndicateServer と組み合わせる。
#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID = "43a2c560-71b4-49bd-9003-696e64696361";
static constexpr const char *CHARACTERISTIC_UUID = "43a2c561-71b4-49bd-9003-696e64696361";

EspBleBluedroid bluetooth;
bool connectionRequested = false;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "Bluedroid Indicate Client";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    // en: notifications=false subscribes to indications (writes 0x0002 to the CCCD).
    // ja: notifications=false でIndicationを購読する（CCCDへ0x0002を書き込む）。
    if (!bluetooth.subscribe(connection.id, SERVICE_UUID, CHARACTERISTIC_UUID, false))
    {
      Serial.printf("Subscribe request failed: %s\n", bluetooth.lastErrorDetail().c_str());
    }
  });
  bluetooth.onSubscribed([](const EspBleGattResult &result) {
    if (!result.success)
    {
      Serial.printf("Subscribe failed: %s\n", result.detail.c_str());
    }
  });
  // en: onNotification receives both; notification.indication tells them apart.
  // ja: onNotification は両方を受信し、notification.indication で区別する。
  bluetooth.onNotification([](const EspBleGattNotification &notification) {
    Serial.printf(
      "%s: %s\n",
      notification.indication ? "Indication" : "Notification",
      notification.value.c_str());
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
  bluetooth.update();
  delay(1);
}
