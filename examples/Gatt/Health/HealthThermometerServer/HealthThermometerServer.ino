// en: HealthThermometerServer - standard Health Thermometer Service (0x1809).
//     Temperature Measurement (0x2A1C) is indicated as an IEEE-11073 32-bit
//     FLOAT; Temperature Type (0x2A1D) is readable. Pairs with
//     HealthThermometerClient or any Health Thermometer collector.
// ja: HealthThermometerServer - 標準Health Thermometer Service（0x1809）。
//     Temperature Measurement（0x2A1C）をIEEE-11073 32-bit FLOATでIndicateし、
//     Temperature Type（0x2A1D）はReadできる。HealthThermometerClientや
//     Health Thermometer collectorと接続できる。
#include <EspBleBluedroid.h>
#include <EspBleMedicalFloat.h>

static constexpr const char *HEALTH_THERMOMETER_SERVICE_UUID = "1809";
static constexpr const char *TEMPERATURE_MEASUREMENT_UUID = "2a1c";
static constexpr const char *TEMPERATURE_TYPE_UUID = "2a1d";

EspBleBluedroid bluetooth;
EspBleGattService healthThermometerServiceService;
EspBleGattCharacteristic temperatureMeasurementCharacteristic;
EspBleGattCharacteristic temperatureTypeCharacteristic;
const uint8_t temperatureType = 0x02; // en: Body / ja: 体温
int16_t temperatureHundredths = 3650; // en: 36.50 C / ja: 36.50 度
unsigned long lastUpdate = 0;

void setup()
{
  Serial.begin(115200);

  EspBleGattCharacteristicConfig measurementConfig;
  measurementConfig.indicatable = true;
  EspBleGattCharacteristicConfig typeConfig;
  typeConfig.readable = true;
  auto &server = bluetooth.gattServer();
  healthThermometerServiceService = server.addService(HEALTH_THERMOMETER_SERVICE_UUID);
  temperatureMeasurementCharacteristic = server.addCharacteristic(healthThermometerServiceService, TEMPERATURE_MEASUREMENT_UUID, measurementConfig);
  temperatureTypeCharacteristic = server.addCharacteristic(healthThermometerServiceService, TEMPERATURE_TYPE_UUID, typeConfig);
  server.setValue(temperatureTypeCharacteristic, &temperatureType, 1);

  EspBleConfig config;
  config.deviceName = "Bluedroid Thermometer";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }
  bluetooth.advertising().addServiceUuid(HEALTH_THERMOMETER_SERVICE_UUID);
  bluetooth.advertising().start();
}

void loop()
{
  // en: Every 2 seconds, indicate a slowly rising temperature (36.50-38.49 C).
  // ja: 2秒ごとに、緩やかに上昇する温度（36.50〜38.49度）をIndicateする。
  if (millis() - lastUpdate >= 2000)
  {
    lastUpdate = millis();
    temperatureHundredths += 10;
    if (temperatureHundredths > 3849)
      temperatureHundredths = 3650;

    uint8_t measurement[5];
    measurement[0] = 0x00; // en: Celsius, no timestamp/type / ja: 摂氏、timestamp/type無し
    espBleWriteMedicalFloat32LE(&measurement[1], temperatureHundredths, -2);
    bluetooth.gattServer().setValue(temperatureMeasurementCharacteristic, measurement, sizeof(measurement));
    // en: indicate() only reaches subscribers; it is a no-op with none.
    // ja: indicate()は購読者にのみ届く。購読者がいなければ何もしない。
    bluetooth.gattServer().indicate(temperatureMeasurementCharacteristic, measurement, sizeof(measurement));
  }

  bluetooth.update();
  delay(1);
}
