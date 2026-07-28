// en: Client - connect to a compatible custom GATT Server and run the central
//     flow: database discovery -> read -> writes -> descriptor access.
// ja: Client - 対応する独自GATT Serverへ接続し、CentralのGATT Clientフロー
//     （Database Discovery → Read → Write → Descriptor操作）を実行する。
#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID =
  "10da4dd0-8eaa-4c69-9003-676174747277";
static constexpr const char *CHARACTERISTIC_UUID =
  "10da4dd1-8eaa-4c69-9003-676174747277";
static constexpr const char *DESCRIPTOR_UUID =
  "10da4dd2-8eaa-4c69-9003-676174747277";

EspBleBluedroid bluetooth;
bool connectionRequested = false;
uint16_t targetCharacteristicHandle = 0;
unsigned writePhase = 0;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid GATT Client";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n",
      bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    if (!bluetooth.discoverServices(connection.id))
    {
      Serial.printf("Discovery request failed: %s\n",
        bluetooth.lastErrorDetail().c_str());
    }
  });
  bluetooth.onServicesDiscovered([](const EspBleGattResult &result) {
    if (!result.success)
    {
      Serial.printf("Database discovery failed: %s\n",
        result.detail.c_str());
      return;
    }
    Serial.printf("Services: %u, characteristics: %u, descriptors: %u\n",
      static_cast<unsigned>(
        bluetooth.discoveredServiceCount(result.connectionId)),
      static_cast<unsigned>(
        bluetooth.discoveredCharacteristicCount(result.connectionId)),
      static_cast<unsigned>(
        bluetooth.discoveredDescriptorCount(result.connectionId)));

    const size_t count = bluetooth.discoveredCharacteristicCount(
      result.connectionId, SERVICE_UUID);
    for (size_t index = 0; index < count; ++index)
    {
      EspBleGattCharacteristicInfo info;
      if (bluetooth.discoveredCharacteristic(
            result.connectionId, index, info, SERVICE_UUID) &&
          info.characteristicUuid.equalsIgnoreCase(CHARACTERISTIC_UUID))
      {
        targetCharacteristicHandle = info.handle;
        break;
      }
    }
    if (targetCharacteristicHandle == 0)
    {
      Serial.println("Target characteristic was not found");
      return;
    }
    bluetooth.readCharacteristic(
      result.connectionId, targetCharacteristicHandle);
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    if (!result.success)
    {
      Serial.printf("Read failed: %s\n", result.detail.c_str());
      return;
    }
    Serial.printf("Read: %s\n", result.value.c_str());
    bluetooth.writeCharacteristic(
      result.connectionId, result.handle,
      String("hello from Central"), true);
  });
  bluetooth.onCharacteristicWritten([](const EspBleGattResult &result) {
    if (!result.success)
    {
      Serial.printf("Write failed: %s\n", result.detail.c_str());
      return;
    }
    if (writePhase++ == 0)
    {
      bluetooth.writeCharacteristic(
        result.connectionId, result.handle,
        String("unacknowledged Central write"), false);
    }
    else
    {
      bluetooth.readDescriptor(
        result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID,
        DESCRIPTOR_UUID);
    }
  });
  bluetooth.onDescriptorRead([](const EspBleGattResult &result) {
    if (!result.success)
    {
      Serial.printf("Descriptor read failed: %s\n", result.detail.c_str());
      return;
    }
    Serial.printf("Descriptor: %s\n", result.value.c_str());
    bluetooth.writeDescriptor(
      result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID,
      DESCRIPTOR_UUID, String("updated description"), true);
  });
  bluetooth.onDescriptorWritten([](const EspBleGattResult &result) {
    Serial.println(result.success
      ? "Descriptor write complete" : "Descriptor write failed");
  });

  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested || !result.advertisesService(SERVICE_UUID)) return;
    bluetooth.scanner().stop();
    connectionRequested = bluetooth.connect(result);
  });

  EspBleScanConfig scanConfig;
  scanConfig.active = true;
  if (!bluetooth.scanner().start(scanConfig))
  {
    Serial.printf("Scan start failed: %s\n",
      bluetooth.lastErrorDetail().c_str());
  }
}

void loop()
{
  bluetooth.update();
  delay(1);
}
