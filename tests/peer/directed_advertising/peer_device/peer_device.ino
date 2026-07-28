#include <EspBleBluedroid.h>
#include <BLEDevice.h>
#include <BLEServer.h>

static constexpr const char *SERVICE_UUID =
  "c32f5a42-6317-4de1-a77a-52b6e4c574e7";

EspBleBluedroid bluetooth;
BLEServer *server = nullptr;

void setup()
{
  Serial.begin(115200);
  Serial.setTimeout(3000);
  delay(500);

  const bool prebeginRejected = !bluetooth.advertising().startDirected(
    "02:00:00:00:00:01", EspBleAddressType::Public);
  if (!bluetooth.begin())
  {
    Serial.printf("BEGIN_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }
  server = BLEDevice::createServer();
  server->createService(SERVICE_UUID)->start();

  bluetooth.advertising().setName("not allowed");
  const bool payloadRejected = !bluetooth.advertising().startDirected(
    "02:00:00:00:00:01", EspBleAddressType::Public);
  bluetooth.advertising().clear();
  const bool invalidAddressRejected =
    !bluetooth.advertising().startDirected(
      "invalid", EspBleAddressType::Public);
  Serial.printf(
    "VALIDATION prebegin=%u payload=%u invalid_address=%u\n",
    prebeginRejected ? 1 : 0,
    payloadRejected ? 1 : 0,
    invalidAddressRejected ? 1 : 0);
  Serial.printf("PERIPHERAL_READY address=%s\n",
    bluetooth.localAddress().c_str());
}

void loop()
{
  if (Serial.available() > 0)
  {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command == "?")
    {
      Serial.printf("ADVERTISING %u\n",
        bluetooth.advertising().isAdvertising() ? 1 : 0);
    }
    else if (command == "x")
    {
      const bool stopped = bluetooth.advertising().stop();
      Serial.printf("DIRECTED_STOPPED success=%u advertising=%u\n",
        stopped ? 1 : 0,
        bluetooth.advertising().isAdvertising() ? 1 : 0);
    }
    else if (command.length() == 18 &&
             (command[0] == 'h' || command[0] == 'l'))
    {
      const bool highDuty = command[0] == 'h';
      const String target = command.substring(1);
      const bool started = bluetooth.advertising().startDirected(
        target.c_str(),
        EspBleAddressType::Public,
        highDuty ? EspBleDirectedAdvertisingMode::HighDutyCycle
                 : EspBleDirectedAdvertisingMode::LowDutyCycle);
      Serial.printf(
        "DIRECTED_STARTED success=%u mode=%s target=%s advertising=%u error=%s\n",
        started ? 1 : 0,
        highDuty ? "high" : "low",
        target.c_str(),
        bluetooth.advertising().isAdvertising() ? 1 : 0,
        bluetooth.lastErrorName());
    }
  }
  bluetooth.update();
  delay(1);
}
