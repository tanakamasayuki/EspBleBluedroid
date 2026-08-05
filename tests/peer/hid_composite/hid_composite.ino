// Every HID device profile in one device: keyboard, mouse, consumer control,
// system control and gamepad.
//
// HOGP puts them in a single HID service and tells their reports apart by Report
// ID, which is the part this suite exists for: one Report Map that is the
// concatenation of the five descriptors, five Input Report characteristics sharing
// UUID 0x2A4D with a Report Reference each, and a notification that arrives on the
// right one. The mouse is configured with three buttons rather than the default
// five, so the patched field is checked where it matters — on the air.
//
// The HID UUIDs are fixed by the specification, so isolation is by device name.

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *DEVICE_NAME = "Bluedroid HID 000d";
static constexpr uint8_t MOUSE_BUTTONS = 3;

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
bool started = false;
bool configured = false;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();

  // Every profile before begin(): they share one HID service, so the Report Map
  // and the attribute table are built from whatever has been configured.
  EspBleHidKeyboardConfig keyboardConfig;
  keyboardConfig.manufacturer = "EspBleBluedroid";
  EspBleHidMouseConfig mouseConfig;
  mouseConfig.buttons = MOUSE_BUTTONS;
  configured = bluetooth.hidKeyboard().configure(keyboardConfig) &&
    bluetooth.hidMouse().configure(mouseConfig) &&
    bluetooth.hidConsumerControl().configure() &&
    bluetooth.hidSystemControl().configure() &&
    bluetooth.hidGamepad().configure();
  if (!configured)
  {
    Serial.printf("CONFIGURE_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }

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
  Serial.println("HID_COMPOSITE_READY");
}

void loop()
{
  bluetooth.update();
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.printf(
        "READY_STATE started=%u configured=%u keyboard=%u mouse=%u consumer=%u "
        "system=%u gamepad=%u\n",
        started ? 1 : 0, configured ? 1 : 0,
        bluetooth.hidKeyboard().ready() ? 1 : 0,
        bluetooth.hidMouse().ready() ? 1 : 0,
        bluetooth.hidConsumerControl().ready() ? 1 : 0,
        bluetooth.hidSystemControl().ready() ? 1 : 0,
        bluetooth.hidGamepad().ready() ? 1 : 0);
    }
    else if (command == 'k')
    {
      EspBleHidKeyboardReport report;
      report.modifiers = EspBleHidKeyboardReport::LeftShift;
      report.keys[0] = 0x04;
      Serial.printf("SEND keyboard=%u error=%s\n",
        bluetooth.hidKeyboard().sendReport(report) ? 1 : 0,
        bluetooth.lastErrorName());
    }
    else if (command == 'm')
    {
      // Right button held while moving, which is also the drag case: move() keeps
      // the buttons that are down.
      Serial.printf("SEND mouse=%u error=%s\n",
        bluetooth.hidMouse().press(ESP_BLE_HID_MOUSE_RIGHT) ? 1 : 0,
        bluetooth.lastErrorName());
      Serial.printf("SEND move=%u buttons=0x%02x\n",
        bluetooth.hidMouse().move(5, -5, 1) ? 1 : 0,
        bluetooth.hidMouse().buttons());
    }
    else if (command == 'M')
    {
      Serial.printf("SEND mouse_release=%u buttons=0x%02x\n",
        bluetooth.hidMouse().releaseAll() ? 1 : 0,
        bluetooth.hidMouse().buttons());
    }
    else if (command == 'c')
    {
      // Volume Up (Consumer page usage 0x00e9), one 16-bit usage per report.
      Serial.printf("SEND consumer=%u usage=%u\n",
        bluetooth.hidConsumerControl().press(0x00e9) ? 1 : 0,
        bluetooth.hidConsumerControl().usage());
    }
    else if (command == 'C')
    {
      Serial.printf("SEND consumer_release=%u usage=%u\n",
        bluetooth.hidConsumerControl().release() ? 1 : 0,
        bluetooth.hidConsumerControl().usage());
    }
    else if (command == 's')
    {
      // System Sleep (usage 0x82 in the descriptor's 0..3 range: index 2).
      Serial.printf("SEND system=%u usage=%u\n",
        bluetooth.hidSystemControl().press(0x02) ? 1 : 0,
        bluetooth.hidSystemControl().usage());
    }
    else if (command == 'g')
    {
      Serial.printf("SEND gamepad=%u error=%s\n",
        bluetooth.hidGamepad().send(1, -2, 3, -4, 5, -6,
          ESP_BLE_HID_GAMEPAD_HAT_RIGHT, 0x00010203u) ? 1 : 0,
        bluetooth.lastErrorName());
    }
  }
  delay(1);
}
