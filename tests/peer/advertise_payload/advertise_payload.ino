// Inspect raw advertising data with the bundled Bluedroid API. This side does
// not use EspBleBluedroid, so it can independently validate the wire payload.
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>

BLEScan *scan = nullptr;

class Callbacks : public BLEAdvertisedDeviceCallbacks
{
public:
  void onResult(BLEAdvertisedDevice device) override
  {
    if (!device.haveName() || String(device.getName().c_str()) != "BlePayload")
    {
      return;
    }
    uint8_t *payload = device.getPayload();
    const size_t length = device.getPayloadLength();
    String hex;
    for (size_t index = 0; index < length; ++index)
    {
      char buffer[3];
      snprintf(buffer, sizeof(buffer), "%02x", payload[index]);
      hex += buffer;
    }
    Serial.printf("SCANNER_PAYLOAD len=%u hex=%s\n",
      static_cast<unsigned>(length), hex.c_str());
    scan->stop();
  }
};

Callbacks callbacks;

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid Payload Scanner");
  scan = BLEDevice::getScan();
  Serial.println("SCANNER_READY");
}

void loop()
{
  if (Serial.available() > 0 && Serial.read() == 's')
  {
    scan->setAdvertisedDeviceCallbacks(&callbacks, true, true);
    scan->setActiveScan(false);
    scan->setInterval(100);
    scan->setWindow(50);
    Serial.printf("SCANNER_STARTED success=%u\n",
      scan->start(0, nullptr, false) ? 1 : 0);
  }
  delay(1);
}
