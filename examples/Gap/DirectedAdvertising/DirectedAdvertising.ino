// en: Advertise directly to one known central. Directed Advertising carries
//     no name, service UUID, manufacturer data, or scan response.
// ja: 既知のCentral 1台へ直接advertiseする。Directed Advertisingにはname、
//     Service UUID、Manufacturer Data、Scan Responseを一切載せられない。
#include <EspBleBluedroid.h>

// en: Replace this with the target central's identity address.
// ja: 接続先Centralのidentity addressへ書き換える。
static constexpr const char *TARGET_CENTRAL = "aa:bb:cc:dd:ee:ff";

EspBleBluedroid bluetooth;

bool startDirected(EspBleDirectedAdvertisingMode mode)
{
  if (!bluetooth.advertising().startDirected(
        TARGET_CENTRAL, EspBleAddressType::Public, mode))
  {
    Serial.printf("Directed Advertising failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return false;
  }
  Serial.printf("Directed Advertising started: %s -> %s\n",
    mode == EspBleDirectedAdvertisingMode::HighDutyCycle ? "high" : "low",
    TARGET_CENTRAL);
  return true;
}

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "Bluedroid Directed";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE init failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  // en: High Duty uses the fixed 3.75 ms interval and stops after at most
  //     1.28 seconds if the target does not connect.
  // ja: High Dutyは固定3.75 ms間隔で、targetが接続しなければ最大1.28秒で停止する。
  startDirected(EspBleDirectedAdvertisingMode::HighDutyCycle);
  Serial.println("Send h=high, l=low, x=stop.");
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'x')
    {
      Serial.println(bluetooth.advertising().stop()
        ? "Directed Advertising stopped" : "Stop failed");
    }
    else if (command == 'h' || command == 'l')
    {
      bluetooth.advertising().stop();
      startDirected(
        command == 'h'
          ? EspBleDirectedAdvertisingMode::HighDutyCycle
          : EspBleDirectedAdvertisingMode::LowDutyCycle);
    }
  }

  bluetooth.update();
  delay(1);
}
