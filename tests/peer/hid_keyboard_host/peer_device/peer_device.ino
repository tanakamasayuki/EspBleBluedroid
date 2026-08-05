// The HID keyboard device the host under test consumes. This library's own device
// side, configured with values a host can read back and check: a country code, a
// battery level, and a manufacturer name that are all deliberately non-default.
//
// peer/hid_keyboard_device already pins this device against a raw central, so using
// it here checks the host rather than re-checking the device.

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *DEVICE_NAME = "Bluedroid HID 0010";
static constexpr uint8_t COUNTRY_CODE = 33;
static constexpr uint8_t BATTERY_LEVEL = 73;

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
bool started = false;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();

  auto &keyboard = bluetooth.hidKeyboard();
  EspBleHidKeyboardConfig keyboardConfig;
  keyboardConfig.manufacturer = "Bluedroid Host Peer";
  keyboardConfig.vendorId = 0x303a;
  keyboardConfig.productId = 0x4002;
  keyboardConfig.productVersion = 0x0100;
  keyboardConfig.countryCode = COUNTRY_CODE;
  keyboardConfig.initialBatteryLevel = BATTERY_LEVEL;
  if (!keyboard.configure(keyboardConfig))
  {
    Serial.printf("DEVICE_CONFIG_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }
  keyboard.onOutputReport([](const EspBleHidKeyboardOutputReport &report) {
    Serial.printf("DEVICE_LED leds=0x%02x caps=%u context=%s\n", report.leds,
      report.capsLock ? 1 : 0, contextName());
  });

  EspBleConfig config;
  config.deviceName = DEVICE_NAME;
  if (!bluetooth.begin(config))
  {
    Serial.printf("DEVICE_BEGIN_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }
  bluetooth.advertising().setName(DEVICE_NAME);
  if (!bluetooth.advertising().start())
  {
    Serial.printf("DEVICE_ADVERTISE_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  started = true;
  Serial.println("DEVICE_READY");
}

void loop()
{
  bluetooth.update();
  if (Serial.available())
  {
    const char command = Serial.read();
    auto &keyboard = bluetooth.hidKeyboard();
    if (command == '?')
    {
      Serial.printf("DEVICE_STATE started=%u ready=%u caps=%u\n", started ? 1 : 0,
        keyboard.ready() ? 1 : 0, keyboard.ledState().capsLock ? 1 : 0);
    }
    else if (command == 'a')
    {
      // Shift+A, the report a host has to decode into usage 0x04 with the shift
      // modifier and the character 'A'.
      EspBleHidKeyboardReport report;
      report.modifiers = EspBleHidKeyboardReport::LeftShift;
      report.keys[0] = 0x04;
      Serial.printf("DEVICE_SEND press=%u error=%s\n",
        keyboard.sendReport(report) ? 1 : 0, bluetooth.lastErrorName());
    }
    else if (command == 'b')
    {
      // Two keys at once, so the host's state has both down while its events name
      // only the one that changed.
      EspBleHidKeyboardReport report;
      report.modifiers = EspBleHidKeyboardReport::LeftShift;
      report.keys[0] = 0x04;
      report.keys[1] = 0x05;
      Serial.printf("DEVICE_SEND two=%u error=%s\n",
        keyboard.sendReport(report) ? 1 : 0, bluetooth.lastErrorName());
    }
    else if (command == 'r')
    {
      Serial.printf("DEVICE_SEND release=%u error=%s\n",
        keyboard.releaseAll() ? 1 : 0, bluetooth.lastErrorName());
    }
    else if (command == 'E')
    {
      // A key slot holding 0x02 (POSTFail). Usages 0x01-0x03 are HID error codes,
      // not keys, and sendReport() passes the array through as given — which is
      // what makes this reachable from the public API at all. A host must not
      // report usage 2 as a pressed key.
      EspBleHidKeyboardReport report;
      report.keys[0] = 0x02;
      Serial.printf("DEVICE_SEND error_code=%u error=%s\n",
        keyboard.sendReport(report) ? 1 : 0, bluetooth.lastErrorName());
    }
  }
  delay(1);
}
