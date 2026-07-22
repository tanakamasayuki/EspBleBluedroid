#include <EspBleBluedroid.h>

EspBleBluedroid ble;

void printStart(const char *label, bool success)
{
  Serial.printf("%s success=%u error=%s\n", label, success ? 1 : 0,
    ble.lastErrorName());
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  EspBleConfig config;
  config.deviceName = "BlePayload";
  if (!ble.begin(config))
  {
    Serial.printf("DEVICE_INIT_FAILED %s %s\n",
      ble.lastErrorName(), ble.lastErrorDetail().c_str());
    return;
  }
  Serial.println("DEVICE_READY");
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.printf("DEVICE_ADVERTISING %u\n",
        ble.advertising().isAdvertising() ? 1 : 0);
    }
    else if (command == 'q')
    {
      printStart("DEVICE_STOPPED", ble.advertising().stop());
    }
    else if (command == 'a')
    {
      auto &advertising = ble.advertising();
      advertising.stop();
      advertising.clear();
      advertising.setName("BlePayload");
      advertising.setScanResponseEnabled(false);
      const bool added = advertising.addServiceUuid("1812") &&
        advertising.addServiceUuid("180f");
      printStart("DEVICE_MULTI_UUID", added && advertising.start());
    }
    else if (command == 'b' || command == 'x')
    {
      auto &advertising = ble.advertising();
      advertising.stop();
      advertising.clear();
      advertising.setScanResponseEnabled(false);
      // Flags consume 3 bytes; a local-name structure consumes length + 2.
      // 26 bytes fits exactly in 31, while 27 must be rejected.
      advertising.setName(command == 'b'
        ? "12345678901234567890123456"
        : "123456789012345678901234567");
      printStart(command == 'b' ? "DEVICE_BOUNDARY" : "DEVICE_OVERFLOW",
        advertising.start());
    }
    else if (command == 't')
    {
      auto &advertising = ble.advertising();
      advertising.stop();
      advertising.clear();
      advertising.setName("BleTimed");
      printStart("DEVICE_TIMED", advertising.start(1));
    }
  }

  ble.update();
  delay(1);
}
