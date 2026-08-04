// en: Mtu - request a larger ATT MTU before connecting and observe the negotiated value.
//     The preferred MTU is set in the config passed to begin(); the backend exchanges
//     it during connection establishment.
// ja: Mtu - 接続前に大きめのATT MTUを希望値として設定し、交換結果を観察する。
//     希望MTUは begin() へ渡すconfigで指定し、backendが接続確立時に交換する。
#include <EspBleBluedroid.h>

// en: Matches the service UUID of the Gatt/Basics/NotifyServer example.
// ja: Gatt/Basics/NotifyServer example のService UUIDに合わせてある。
static constexpr const char *TARGET_SERVICE_UUID = "71756360-5fa4-43bc-9003-6e6f74696679";

EspBleBluedroid bluetooth;
bool connectionRequested = false;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "Bluedroid MTU Central";
  // en: preferred ATT MTU (23-517; out of range is rejected by begin())
  // ja: 希望ATT MTU（23〜517。範囲外は begin() が拒否）
  config.preferredMtu = 185;
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    // en: connection.mtu is the snapshot taken at connection; maximumNotificationPayload()
    //     is mtu-3 (excluding the ATT notification header).
    // ja: connection.mtu は接続完了時のsnapshot。maximumNotificationPayload() は
    //     mtu-3（ATT notification header分を除いた値）。
    Serial.printf(
      "Connected with MTU %u (notification payload up to %u bytes)\n",
      connection.mtu,
      static_cast<unsigned>(connection.maximumNotificationPayload()));
  });
  bluetooth.onMtuChanged([](const EspBleMtuChanged &event) {
    Serial.printf("MTU changed from %u to %u\n", event.previousMtu, event.connection.mtu);
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &scanResult) {
    if (connectionRequested || !scanResult.advertisesService(TARGET_SERVICE_UUID))
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
