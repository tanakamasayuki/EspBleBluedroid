// HID over GATT Boot Protocol: the fixed 8-byte keyboard report a host uses before
// it can parse a Report Descriptor (a BIOS, a boot menu).
//
// The keyboard here is NKRO, which is what makes the suite worth having: in Report
// Protocol Mode its report is the 29-byte bitmap the Report Map declares, and in
// Boot Protocol Mode the *same* send has to come out as [modifiers, reserved,
// keycode1..6] — the library down-converts the bitmap, and more than six held keys
// become the HID rollover code 0x01. Nothing else exercises that conversion.
//
// Boot Protocol is opt-in (EspBleHidKeyboardConfig::bootProtocol), because the two
// extra characteristics enlarge every host's discovery.
//
// Security is deliberately off: what a host may see before pairing is
// peer/hid_security's subject, and leaving it off here keeps the instrument a plain
// central.
//
// The HID UUIDs are fixed by the specification, so isolation is by device name.

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *DEVICE_NAME = "Bluedroid HID 000f";
// Three usages that fit a boot report, and seven that cannot: 'a', 'b' and '1'
// then four more, so the rollover path is reached by one key rather than by many.
static const uint8_t THREE_KEYS[] = {0x04, 0x05, 0x1e};
static const uint8_t SEVEN_KEYS[] = {0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a};

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
bool started = false;
bool configured = false;
uint16_t negotiatedMtu = 23;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

bool sendUsages(const uint8_t *usages, size_t count)
{
  EspBleHidKeyboardNkroReport report;
  for (size_t index = 0; index < count; ++index) report.press(usages[index]);
  return bluetooth.hidKeyboard().sendReport(report);
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();

  auto &keyboard = bluetooth.hidKeyboard();
  // Both before configure(): NKRO selects the descriptor that goes into the Report
  // Map, and bootProtocol adds the two Boot Keyboard characteristics.
  keyboard.enableNkro();
  EspBleHidKeyboardConfig keyboardConfig;
  keyboardConfig.manufacturer = "EspBleBluedroid";
  keyboardConfig.bootProtocol = true;
  configured = keyboard.configure(keyboardConfig);
  if (!configured)
  {
    Serial.printf("CONFIGURE_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }

  keyboard.onProtocolMode([](uint8_t mode, EspBleConnectionId connectionId) {
    Serial.printf("PROTOCOL_MODE mode=%u id=%u context=%s\n", mode,
      static_cast<unsigned>(connectionId), contextName());
  });
  // The LED report a host writes in Boot Protocol Mode goes to the Boot Keyboard
  // Output Report, and has to reach the same callback as the Report-protocol one.
  keyboard.onOutputReport([](const EspBleHidKeyboardOutputReport &report) {
    Serial.printf("LED_STATE leds=0x%02x caps=%u context=%s\n", report.leds,
      report.capsLock ? 1 : 0, contextName());
  });
  bluetooth.onMtuChanged([](const EspBleMtuChanged &event) {
    negotiatedMtu = event.connection.mtu;
  });

  EspBleConfig config;
  config.deviceName = DEVICE_NAME;
  if (!bluetooth.begin(config))
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
  Serial.println("HID_BOOT_PROTOCOL_READY");
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
      // mode is the Host's choice, ready() follows whichever Input Report that
      // choice makes the live one — the Boot Keyboard CCCD in Boot Protocol Mode.
      Serial.printf(
        "READY_STATE started=%u nkro=%u mode=%u ready=%u mtu=%u\n",
        started ? 1 : 0, keyboard.nkroEnabled() ? 1 : 0, keyboard.protocolMode(),
        keyboard.ready() ? 1 : 0, negotiatedMtu);
    }
    else if (command == '3')
    {
      Serial.printf("SEND three=%u error=%s\n",
        sendUsages(THREE_KEYS, sizeof(THREE_KEYS)) ? 1 : 0,
        bluetooth.lastErrorName());
    }
    else if (command == '7')
    {
      Serial.printf("SEND seven=%u error=%s\n",
        sendUsages(SEVEN_KEYS, sizeof(SEVEN_KEYS)) ? 1 : 0,
        bluetooth.lastErrorName());
    }
    else if (command == 'r')
    {
      Serial.printf("SEND release=%u error=%s\n",
        keyboard.releaseAll() ? 1 : 0, bluetooth.lastErrorName());
    }
  }
  delay(1);
}
