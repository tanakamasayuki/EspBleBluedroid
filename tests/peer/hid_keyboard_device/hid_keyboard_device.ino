// HID over GATT keyboard device against a raw Arduino-ESP32 central standing in
// for a host OS.
//
// `tests/unit/hid_report_maps` pins the Report Descriptor bytes on the host side,
// but bytes in a table prove nothing about what a peer can read: this suite checks
// that the same descriptor reaches the air, that the two 0x2A4D Report
// characteristics are distinguishable by their Report Reference descriptors (the
// duplicate-UUID shape `peer/duplicate_uuid_server` made possible), that an input
// report notification carries the 8-byte keyboard layout, that a host's LED write
// comes back through onOutputReport() and ledState(), and that a Protocol Mode
// write is reported.
//
// Security is left off here so the instrument can stay a plain central; the
// encrypted-attribute half of HOGP belongs with the security suites.
//
// The HID UUIDs are fixed by the specification, so isolation from a test on a
// neighbouring bench is by device name (tests/TEST_PLAN.md).

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *DEVICE_NAME = "Bluedroid HID 000c";

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
  keyboard.onOutputReport([](const EspBleHidKeyboardOutputReport &report) {
    Serial.printf(
      "OUTPUT_REPORT id=%u leds=0x%02x num=%u caps=%u scroll=%u context=%s\n",
      static_cast<unsigned>(report.connectionId), report.leds,
      report.numLock ? 1 : 0, report.capsLock ? 1 : 0,
      report.scrollLock ? 1 : 0, contextName());
  });
  keyboard.onProtocolMode([](uint8_t mode, EspBleConnectionId connectionId) {
    Serial.printf("PROTOCOL_MODE mode=%u id=%u context=%s\n", mode,
      static_cast<unsigned>(connectionId), contextName());
  });

  // Before begin(): configure() registers the HID service, its attributes and the
  // advertised service UUID.
  EspBleHidKeyboardConfig config;
  config.manufacturer = "EspBleBluedroid";
  config.vendorId = 0x1234;
  config.productId = 0xabcd;
  config.productVersion = 0x0102;
  config.initialBatteryLevel = 77;
  if (!keyboard.configure(config))
  {
    Serial.printf("CONFIGURE_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }

  EspBleConfig bleConfig;
  bleConfig.deviceName = DEVICE_NAME;
  if (!bluetooth.begin(bleConfig))
  {
    Serial.printf("BEGIN_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }
  if (!bluetooth.advertising().start())
  {
    Serial.printf("ADVERTISE_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  started = true;
  Serial.println("HID_KEYBOARD_READY");
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
      // Requested rather than printed at boot: the first output is lost while the
      // other board is flashed.
      Serial.printf("READY_STATE started=%u configured=%u ready=%u\n",
        started ? 1 : 0, keyboard.configured() ? 1 : 0,
        keyboard.ready() ? 1 : 0);
    }
    else if (command == 'k')
    {
      // Shift + 'a' as an explicit report, so the test can assert the exact wire
      // bytes rather than trusting a layout lookup.
      EspBleHidKeyboardReport report;
      report.modifiers = EspBleHidKeyboardReport::LeftShift;
      report.keys[0] = 0x04;
      Serial.printf("SEND accepted=%u error=%s\n",
        keyboard.sendReport(report) ? 1 : 0, bluetooth.lastErrorName());
    }
    else if (command == 'r')
    {
      Serial.printf("RELEASE accepted=%u\n", keyboard.releaseAll() ? 1 : 0);
    }
    else if (command == 'w')
    {
      // The layout path: 'A' needs Shift, which pressKey() has to work out from
      // the keymap tables.
      Serial.printf("WRITE accepted=%u error=%s\n",
        keyboard.pressKey('A') ? 1 : 0, bluetooth.lastErrorName());
    }
    else if (command == 'l')
    {
      const EspBleHidKeyboardOutputReport report = keyboard.ledState();
      Serial.printf("LED_STATE id=%u leds=0x%02x num=%u caps=%u\n",
        static_cast<unsigned>(report.connectionId), report.leds,
        report.numLock ? 1 : 0, report.capsLock ? 1 : 0);
    }
    else if (command == 'm')
    {
      Serial.printf("MODE mode=%u\n", keyboard.protocolMode());
    }
    else if (command == 'b')
    {
      Serial.printf("BATTERY accepted=%u\n",
        keyboard.setBatteryLevel(42) ? 1 : 0);
    }
    else if (command == 'n')
    {
      // Refused after configure(): the descriptor decides the report layout, and
      // it is already published.
      keyboard.enableNkro(true);
      Serial.printf("NKRO enabled=%u\n", keyboard.nkroEnabled() ? 1 : 0);
    }
  }
  delay(1);
}
