// en: WeightScaleServer - standard Weight Scale Service (0x181D). Weight
//     Measurement (0x2A9D) is indicated as a uint16 at 0.005 kg resolution;
//     Weight Scale Feature (0x2A9E) is a readable uint32.
// ja: WeightScaleServer - 標準Weight Scale Service（0x181D）。Weight Measurement
//     （0x2A9D）を0.005 kg分解能のuint16でIndicateし、Weight Scale Feature
//     （0x2A9E）はuint32としてReadできる。
#include <EspBleBluedroid.h>

static constexpr const char *WEIGHT_SCALE_SERVICE_UUID = "181d";
static constexpr const char *WEIGHT_MEASUREMENT_UUID = "2a9d";
static constexpr const char *WEIGHT_SCALE_FEATURE_UUID = "2a9e";

EspBleBluedroid bluetooth;
EspBleGattService weightScaleServiceService;
EspBleGattCharacteristic weightMeasurementCharacteristic;
EspBleGattCharacteristic weightScaleFeatureCharacteristic;
const uint8_t feature[4] = {0x08, 0x00, 0x00, 0x00};
unsigned long lastUpdate = 0;
uint16_t rawWeight = 14000; // en: 70.000 kg / ja: 70.000 kg

void setup()
{
  Serial.begin(115200);

  EspBleGattCharacteristicConfig measurementConfig;
  measurementConfig.indicatable = true;
  EspBleGattCharacteristicConfig featureConfig;
  featureConfig.readable = true;
  auto &server = bluetooth.gattServer();
  weightScaleServiceService = server.addService(WEIGHT_SCALE_SERVICE_UUID);
  weightMeasurementCharacteristic = server.addCharacteristic(weightScaleServiceService, WEIGHT_MEASUREMENT_UUID, measurementConfig);
  weightScaleFeatureCharacteristic = server.addCharacteristic(weightScaleServiceService, WEIGHT_SCALE_FEATURE_UUID, featureConfig);
  server.setValue(weightScaleFeatureCharacteristic, feature, sizeof(feature));

  EspBleConfig config;
  config.deviceName = "Bluedroid Weight Scale";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }
  bluetooth.advertising().addServiceUuid(WEIGHT_SCALE_SERVICE_UUID);
  bluetooth.advertising().start();
}

void loop()
{
  // en: Every 3 seconds, indicate a weight that drifts around 70 kg.
  // ja: 3秒ごとに、70 kg付近で変化する体重をIndicateする。
  if (millis() - lastUpdate >= 3000)
  {
    lastUpdate = millis();
    rawWeight += 20; // en: +0.1 kg / ja: +0.1 kg
    if (rawWeight > 14200)
      rawWeight = 14000;

    uint8_t measurement[3];
    measurement[0] = 0x00; // en: SI (kg), no optional fields / ja: SI（kg）、optionalなし
    measurement[1] = static_cast<uint8_t>(rawWeight & 0xFF);
    measurement[2] = static_cast<uint8_t>((rawWeight >> 8) & 0xFF);
    bluetooth.gattServer().setValue(weightMeasurementCharacteristic, measurement, sizeof(measurement));
    bluetooth.gattServer().indicate(weightMeasurementCharacteristic, measurement, sizeof(measurement));
  }

  bluetooth.update();
  delay(1);
}
