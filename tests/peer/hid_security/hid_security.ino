// The security tier of HID over GATT: what a host can read from the HID service
// before it has paired, and what it can read after.
//
// HOGP requires Security Mode 1 Level 2 on the HID attributes, and the
// insufficient-encryption error a host gets on an unencrypted link is the whole
// mechanism by which a host OS starts pairing. peer/hid_keyboard_device runs with
// security off so its instrument can stay a plain central; this suite is the other
// half — the same device with security enabled, read by a central that tries
// without encryption first.
//
// This side is the HID keyboard device under test. The instrument is a raw
// Arduino-ESP32 central (peer_device/), because what is being checked is what a
// *host* is allowed to see: an instrument sharing this library's idea of the
// attribute table could not show that.
//
// The HID UUIDs are fixed by the specification, so isolation from a test on a
// neighbouring bench is by device name (tests/TEST_PLAN.md).

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *DEVICE_NAME = "Bluedroid HID 0011";
static constexpr uint8_t COUNTRY_CODE = 33;
static constexpr uint8_t BATTERY_LEVEL = 73;

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
bool started = false;
bool securityObserved = false;

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
  keyboardConfig.manufacturer = "Bluedroid HID Security";
  keyboardConfig.countryCode = COUNTRY_CODE;
  keyboardConfig.initialBatteryLevel = BATTERY_LEVEL;
  if (!keyboard.configure(keyboardConfig))
  {
    Serial.printf("CONFIG_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }
  keyboard.onOutputReport([](const EspBleHidKeyboardOutputReport &report) {
    Serial.printf("LED leds=0x%02x caps=%u context=%s\n", report.leds,
      report.capsLock ? 1 : 0, contextName());
  });

  EspBleConfig config;
  config.deviceName = DEVICE_NAME;
  // The one line that puts every HID attribute behind encryption. Just Works: a
  // keyboard with no display and no keypad cannot do more, and MITM protection is
  // the passkey suites' subject rather than this one's.
  config.security.enabled = true;
  config.security.bonding = true;
  if (!bluetooth.begin(config))
  {
    Serial.printf("BEGIN_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }
  bluetooth.onSecurityChanged([](const EspBleSecurityChanged &event) {
    securityObserved = securityObserved || event.success;
    Serial.printf(
      "SECURITY success=%u encrypted=%u bonded=%u key=%u context=%s\n",
      event.success ? 1 : 0, event.connection.encrypted ? 1 : 0,
      event.connection.bonded ? 1 : 0, event.connection.encryptionKeySize,
      contextName());
  });
  bluetooth.advertising().setName(DEVICE_NAME);
  if (!bluetooth.advertising().start())
  {
    Serial.printf("ADVERTISE_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  started = true;
  Serial.println("HID_SECURITY_READY");
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
      Serial.printf("STATE started=%u ready=%u secured=%u bonds=%u\n",
        started ? 1 : 0, keyboard.ready() ? 1 : 0, securityObserved ? 1 : 0,
        static_cast<unsigned>(bluetooth.bondCount()));
    }
    else if (command == 'c')
    {
      // Bonds outlive a flash, so a run that starts with one left over would find
      // the link already encrypted and prove nothing about the unpaired case.
      const bool cleared = bluetooth.deleteAllBonds();
      securityObserved = false;
      Serial.printf("BONDS_CLEARED success=%u count=%u\n", cleared ? 1 : 0,
        static_cast<unsigned>(bluetooth.bondCount()));
    }
    else if (command == 'A')
    {
      // Advertising stops when a peer connects and this library does not publish a
      // peripheral disconnect event, so a second connection needs this.
      Serial.printf("ADVERTISE restarted=%u\n",
        bluetooth.advertising().start() ? 1 : 0);
    }
    else if (command == 'a')
    {
      EspBleHidKeyboardReport report;
      report.modifiers = EspBleHidKeyboardReport::LeftShift;
      report.keys[0] = 0x04;
      Serial.printf("SEND press=%u error=%s\n",
        keyboard.sendReport(report) ? 1 : 0, bluetooth.lastErrorName());
    }
    else if (command == 'r')
    {
      Serial.printf("SEND release=%u error=%s\n",
        keyboard.releaseAll() ? 1 : 0, bluetooth.lastErrorName());
    }
  }
  delay(1);
}
