// en: ContinuousGlucoseMonitoringClient - connect to a CGM Service (0x181F),
//     read and E2E-CRC-verify CGM Feature, subscribe to CGM Measurement, verify
//     each measurement's E2E-CRC (CRC-16/MCRF4XX via EspBleCgmCrc.h), and decode
//     the SFLOAT glucose concentration and time offset.
// ja: ContinuousGlucoseMonitoringClient - CGM Service（0x181F）へ接続し、CGM
//     FeatureをReadしてE2E-CRC検証、CGM Measurementを購読、各測定値のE2E-CRC
//     （CRC-16/MCRF4XX、EspBleCgmCrc.h）を検証し、SFLOAT血糖値とtime offsetを
//     デコードする。
#include <EspBleBluedroid.h>
#include <EspBleCgmCrc.h>
#include <EspBleMedicalFloat.h>
#include <math.h>

static constexpr const char *CGM_SERVICE_UUID = "181f";
static constexpr const char *CGM_MEASUREMENT_UUID = "2aa7";
static constexpr const char *CGM_FEATURE_UUID = "2aa8";

EspBleBluedroid bluetooth;

static void copyToBuffer(const String &value, uint8_t *buffer, size_t length)
{
  for (size_t i = 0; i < length; ++i)
    buffer[i] = static_cast<uint8_t>(value[i]);
}

void setup()
{
  Serial.begin(115200);
  if (!bluetooth.begin())
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    bluetooth.readCharacteristic(connection.id, CGM_SERVICE_UUID, CGM_FEATURE_UUID);
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    if (!result.characteristicUuid.equalsIgnoreCase(CGM_FEATURE_UUID) ||
        !result.success || result.value.length() != 6)
      return;
    uint8_t buffer[6];
    copyToBuffer(result.value, buffer, 6);
    if (!espBleCgmVerifyCrc(buffer, 6))
    {
      Serial.println("CGM Feature E2E-CRC mismatch");
      return;
    }
    const uint32_t feature = static_cast<uint32_t>(buffer[0]) |
      (static_cast<uint32_t>(buffer[1]) << 8) | (static_cast<uint32_t>(buffer[2]) << 16);
    Serial.printf("CGM Feature: 0x%06x (E2E-CRC verified)\n", static_cast<unsigned>(feature));
    bluetooth.subscribe(result.connectionId, CGM_SERVICE_UUID, CGM_MEASUREMENT_UUID);
  });
  bluetooth.onNotification([](const EspBleGattNotification &notification) {
    if (!notification.characteristicUuid.equalsIgnoreCase(CGM_MEASUREMENT_UUID) ||
        notification.value.length() != 8)
      return;
    uint8_t buffer[8];
    copyToBuffer(notification.value, buffer, 8);
    if (!espBleCgmVerifyCrc(buffer, 8))
    {
      Serial.println("CGM Measurement E2E-CRC mismatch");
      return;
    }
    const uint8_t sfloat[2] = {buffer[2], buffer[3]};
    const long glucose = lround(espBleReadMedicalSFloatLE(sfloat));
    const unsigned timeOffset = static_cast<unsigned>(buffer[4]) | (static_cast<unsigned>(buffer[5]) << 8);
    Serial.printf("Glucose: %ld mg/dL at +%u min\n", glucose, timeOffset);
  });

  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (result.connectable && result.advertisesService(CGM_SERVICE_UUID))
    {
      bluetooth.scanner().stop();
      bluetooth.connect(result);
    }
  });
  bluetooth.scanner().start();
}

void loop()
{
  bluetooth.update();
  delay(1);
}
