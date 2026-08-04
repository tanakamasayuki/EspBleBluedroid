// Who owns GATT Service Changed (Generic Attribute 0x1801, Characteristic
// 0x2a05) on this backend.
//
// EspBle has notifyServicesChanged(start, end) and expects the application to
// own the Service. Here the stack does: Arduino-ESP32 3.3.11 builds Bluedroid
// with CONFIG_BT_GATTS_SEND_SERVICE_CHANGE_AUTO=y, so Bluedroid publishes
// Generic Attribute itself and sends the indication when the local database
// changes. An application-registered 0x1801 would be a second Service with the
// same UUID that a client resolving by UUID never reaches, and this library
// only allows registration before begin(), so there is no runtime database
// change for the application to announce either.
//
// This sketch therefore registers no Generic Attribute Service at all. It
// advertises a marker Service, and the peer checks that Service Changed is
// present and indicatable anyway — which is the contract an application has to
// know: do not build 0x1801, and do not expect a hook for it.

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// A marker service so the peer can find this board without matching on a name.
static constexpr const char *MARKER_SERVICE_UUID =
  "00020000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *MARKER_CHARACTERISTIC_UUID =
  "00020001-b1dd-4d00-9e5a-627564726f69";

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();

  auto &server = bluetooth.gattServer();
  EspBleGattCharacteristicConfig config;
  config.readable = true;
  const EspBleGattService marker = server.addService(MARKER_SERVICE_UUID);
  const EspBleGattCharacteristic characteristic =
    server.addCharacteristic(marker, MARKER_CHARACTERISTIC_UUID, config);
  const uint8_t value[] = {0x5c, 0x01};
  if (!marker.valid() || !characteristic.valid() ||
      !server.setValue(characteristic, value, sizeof(value)))
  {
    Serial.printf("SERVER_CONFIG_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }

  server.onSubscriptionChanged([](const EspBleGattSubscription &event) {
    Serial.printf(
      "SERVER_SUBSCRIPTION notifications=%u indications=%u context=%s\n",
      event.notifications ? 1 : 0, event.indications ? 1 : 0, contextName());
  });

  EspBleConfig deviceConfig;
  deviceConfig.deviceName = "Bluedroid Service Changed";
  if (!bluetooth.begin(deviceConfig))
  {
    Serial.printf("SERVER_BEGIN_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }
  bluetooth.advertising().setName(deviceConfig.deviceName);
  bluetooth.advertising().addServiceUuid(MARKER_SERVICE_UUID);
  bluetooth.advertising().start();
  // Registered services: the marker only. Generic Attribute is not among them.
  Serial.println("SERVICE_CHANGED_READY registered_generic_attribute=0");
}

void loop()
{
  bluetooth.update();
  delay(1);
}
