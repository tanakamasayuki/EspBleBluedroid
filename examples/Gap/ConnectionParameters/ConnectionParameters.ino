// en: ConnectionParameters - tune a live connection. The parameters that
//     decide responsiveness and power draw are negotiated by the controllers.
//     This sketch shows the initial result and switches between low-latency
//     and low-power profiles.
// ja: ConnectionParameters - 確立済みの接続を調整する。応答性と消費電力を
//     決めるパラメータはcontroller間で交渉される。この例では接続直後の値を表示し、
//     低遅延profileと省電力profileを切り替える。
#include <EspBleBluedroid.h>

// en: Gap/Advertise in this library advertises the Battery Service UUID.
// ja: このライブラリのGap/AdvertiseがBattery Service UUIDをadvertiseする。
static constexpr const char *TARGET_SERVICE_UUID = "180f";

EspBleBluedroid bluetooth;
EspBleConnectionId connectionId = 0;

// en: Units come straight from the BLE specification, so convert them when
//     printing. Interval is in 1.25 ms units and timeout in 10 ms units.
// ja: 単位はBLE仕様そのままなので表示時に換算する。
//     intervalは1.25 ms単位、timeoutは10 ms単位。
static void printParameters(
  const char *label, const EspBleConnection &connection)
{
  Serial.printf(
    "%s interval=%u (%.2f ms) latency=%u timeout=%u (%u ms)\n",
    label,
    connection.connectionInterval,
    connection.connectionInterval * 1.25f,
    connection.peripheralLatency,
    connection.supervisionTimeout,
    static_cast<unsigned>(connection.supervisionTimeout) * 10);
}

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Connection Parameters";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE init failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionId != 0 ||
        !result.advertisesService(TARGET_SERVICE_UUID)) return;
    bluetooth.scanner().stop();
    bluetooth.connect(result);
  });
  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    // en: These are the values selected by the controllers at connection.
    // ja: controllerが接続時に選んだ現在値。
    printParameters("CONNECTED", connection);
    Serial.println("Commands: f fast, s slow, d disconnect");
  });
  // en: The result arrives here. A peer may select values other than those
  //     requested, so the request's return value is not the negotiated result.
  // ja: 変更結果はこちらへ届く。相手が要求と異なる値を選ぶことがあるため、
  //     要求の戻り値は交渉結果ではない。
  bluetooth.onConnectionParametersUpdated(
    [](const EspBleConnection &connection) {
      printParameters("PARAMETERS", connection);
    });
  bluetooth.onConnectionFailed(
    [](const EspBleConnectionFailure &failure) {
      Serial.printf("CONNECT_FAILED peer=%s detail=%s\n",
        failure.peerAddress.c_str(), failure.detail.c_str());
    });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    connectionId = 0;
    Serial.printf("DISCONNECTED id=%lu reason=%d\n",
      static_cast<unsigned long>(connection.id),
      connection.disconnectReason);
  });

  bluetooth.scanner().start();
  Serial.println("Scanning for a Battery Service peripheral...");
}

void loop()
{
  if (Serial.available() > 0 && connectionId != 0)
  {
    const char command = Serial.read();
    if (command == 'f')
    {
      // en: Low latency: 15-30 ms interval, no skipped events, 4 s timeout.
      //     This is responsive, but wakes the radio more frequently.
      // ja: 低遅延: interval 15-30 ms、skipなし、timeout 4秒。
      //     応答は速いが、無線が頻繁に起きるので消費電力が増える。
      Serial.printf("REQUEST fast accepted=%u\n",
        bluetooth.updateConnectionParameters(
          connectionId, 12, 24, 0, 400) ? 1 : 0);
    }
    else if (command == 's')
    {
      // en: Lower power: 400-500 ms interval and up to four skipped events.
      //     The 6 s timeout remains longer than the possible silence.
      // ja: 省電力: interval 400-500 ms、最大4回skip。timeoutは、
      //     Peripheralが沈黙しうる時間より長い6秒にする。
      Serial.printf("REQUEST slow accepted=%u\n",
        bluetooth.updateConnectionParameters(
          connectionId, 320, 400, 4, 600) ? 1 : 0);
    }
    else if (command == 'd')
    {
      bluetooth.disconnect(connectionId);
    }
  }

  bluetooth.update();
  delay(1);
}
