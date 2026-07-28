#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;
bool wantDuplicates = false;

static void printHex(const String &data)
{
  for (size_t index = 0; index < data.length(); ++index)
  {
    Serial.printf("%02x", static_cast<uint8_t>(data[index]));
  }
}

static bool startScan()
{
  bluetooth.scanner().stop();
  EspBleScanConfig config;
  config.active = true;
  config.durationSeconds = 0;
  config.wantDuplicates = wantDuplicates;
  if (!bluetooth.scanner().start(config))
  {
    Serial.printf("Scan failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return false;
  }
  Serial.printf("Scanning. duplicates=%s\n",
    wantDuplicates ? "on" : "off");
  return true;
}

void setup()
{
  Serial.begin(115200);
  if (!bluetooth.begin())
  {
    Serial.printf("BLE init failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    Serial.printf("%s type=%u rssi=%d%s%s",
      result.address.c_str(),
      static_cast<unsigned>(result.addressType),
      result.rssi,
      result.connectable ? " connectable" : "",
      result.scannable ? " scannable" : "");
    if (result.hasName())
    {
      Serial.printf(" name=\"%s\"", result.name.c_str());
    }
    if (result.hasAppearance())
    {
      Serial.printf(" appearance=0x%04x", result.appearance);
    }
    if (result.hasTxPowerLevel())
    {
      Serial.printf(" txpower=%ddBm loss=%ddB",
        static_cast<int>(result.txPowerLevel),
        static_cast<int>(result.txPowerLevel) - result.rssi);
    }
    for (size_t index = 0; index < result.serviceUuidCount; ++index)
    {
      Serial.printf(" uuid=%s", result.serviceUuids[index].c_str());
    }
    for (size_t index = 0; index < result.serviceDataCount; ++index)
    {
      const EspBleServiceData &block = result.serviceData[index];
      Serial.printf(" servicedata[%s][%u]=",
        block.uuid.c_str(), static_cast<unsigned>(block.data.length()));
      printHex(block.data);
    }
    if (result.hasManufacturerData())
    {
      Serial.printf(" manufacturer[%u]=",
        static_cast<unsigned>(result.manufacturerData.length()));
      printHex(result.manufacturerData);
    }
    Serial.println();
  });

  startScan();
  Serial.println("Commands: q counters, d toggle duplicate reporting");
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'q')
    {
      Serial.printf("counters: droppedScanResults=%u droppedEvents=%u\n",
        static_cast<unsigned>(bluetooth.scanner().droppedResultCount()),
        static_cast<unsigned>(bluetooth.droppedEventCount()));
    }
    else if (command == 'd')
    {
      wantDuplicates = !wantDuplicates;
      startScan();
    }
  }

  bluetooth.update();
  delay(1);
}
