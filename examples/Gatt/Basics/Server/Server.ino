#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID =
  "10da4dd0-8eaa-4c69-9003-676174747277";
static constexpr const char *CHARACTERISTIC_UUID =
  "10da4dd1-8eaa-4c69-9003-676174747277";
static constexpr const char *DESCRIPTOR_UUID =
  "10da4dd2-8eaa-4c69-9003-676174747277";

EspBleBluedroid bluetooth;
EspBleGattCharacteristic valueCharacteristic;

void setup()
{
  Serial.begin(115200);
  auto &server = bluetooth.gattServer();
  EspBleGattCharacteristicConfig characteristicConfig;
  characteristicConfig.readable = true;
  characteristicConfig.writable = true;
  characteristicConfig.writableWithoutResponse = true;
  EspBleGattDescriptorConfig descriptorConfig;
  descriptorConfig.writable = true;

  const EspBleGattService service = server.addService(SERVICE_UUID);
  valueCharacteristic = server.addCharacteristic(
    service, CHARACTERISTIC_UUID, characteristicConfig);
  const EspBleGattDescriptor descriptor = server.addDescriptor(
    valueCharacteristic, DESCRIPTOR_UUID, descriptorConfig);
  if (!service || !valueCharacteristic || !descriptor ||
      !server.setValue(valueCharacteristic, String("ready")) ||
      !server.setDescriptorValue(descriptor, String("EspBle value")))
  {
    Serial.printf("GATT configuration failed: %s\n",
      bluetooth.lastErrorDetail().c_str());
    return;
  }

  server.onWritten([](const EspBleGattWrite &write) {
    Serial.printf("Connection %u wrote %u bytes\n",
      static_cast<unsigned>(write.connectionId),
      static_cast<unsigned>(write.value.length()));
  });

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid GATT Server";
  if (!bluetooth.begin(config)) return;
  bluetooth.advertising().setName(config.deviceName);
  bluetooth.advertising().addServiceUuid(SERVICE_UUID);
  bluetooth.advertising().start();
}

void loop()
{
  bluetooth.update();
  delay(1);
}
