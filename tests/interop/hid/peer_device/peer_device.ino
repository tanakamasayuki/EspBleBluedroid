// The EspBleBluedroid half of the cross-stack HID over GATT scenario, on the
// original ESP32. One firmware serves both directions, chosen by the mode command,
// and the role is part of the advertised name (the HID UUIDs are fixed by the
// specification, so they cannot isolate this suite).
//
// Deliberately the same commands and the same printed facts as the EspBle side, so
// the test asserts one set of expectations against both stacks. Output is prefixed
// BLUEDROID_.

#include <EspBleBluedroid.h>

static constexpr const char *DEVICE_NAME = "Bluedroid HID Device 0107";
static constexpr const char *TARGET_NAME = "EspBle HID Device 0107";
static constexpr uint8_t COUNTRY_CODE = 33;
static constexpr uint8_t BATTERY_LEVEL = 73;

EspBleBluedroid bluetooth;
char mode = 0;
EspBleConnectionId connectionId = 0;
bool scanning = false;

void reportKey(const EspBleHidKeyboardEvent &event)
{
  Serial.printf(
    "BLUEDROID_KEY usage=%u ascii=%u pressed=%u released=%u mods=0x%02x "
    "length=%u\n",
    event.usage, event.ascii, event.pressed ? 1 : 0, event.released ? 1 : 0,
    event.modifiers, static_cast<unsigned>(event.rawLength));
}

void printState()
{
  Serial.printf("BLUEDROID_STATE mode=%c id=%u ready=%u\n",
    mode == 0 ? '-' : mode, static_cast<unsigned>(connectionId),
    mode == 'd' ? (bluetooth.hidKeyboard().ready() ? 1 : 0)
                : (bluetooth.hidHost().ready(connectionId) ? 1 : 0));
}

void startDevice()
{
  auto &keyboard = bluetooth.hidKeyboard();
  EspBleHidKeyboardConfig keyboardConfig;
  keyboardConfig.manufacturer = "Bluedroid Interop";
  keyboardConfig.countryCode = COUNTRY_CODE;
  keyboardConfig.initialBatteryLevel = BATTERY_LEVEL;
  if (!keyboard.configure(keyboardConfig))
  {
    Serial.printf("BLUEDROID_CONFIG_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  keyboard.onOutputReport([](const EspBleHidKeyboardOutputReport &report) {
    Serial.printf("BLUEDROID_LED leds=0x%02x caps=%u\n", report.leds,
      report.capsLock ? 1 : 0);
  });

  EspBleConfig config;
  config.deviceName = DEVICE_NAME;
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLUEDROID_BEGIN_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  bluetooth.onDisconnected([](const EspBleConnection &) {
    bluetooth.advertising().start();
  });
  bluetooth.advertising().setName(DEVICE_NAME);
  if (!bluetooth.advertising().start())
  {
    Serial.printf("BLUEDROID_ADVERTISE_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  Serial.println("BLUEDROID_DEVICE_READY");
}

void startHost()
{
  EspBleConfig config;
  config.deviceName = "Bluedroid HID Host 0107";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLUEDROID_BEGIN_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    Serial.printf("BLUEDROID_CONNECTED id=%u\n",
      static_cast<unsigned>(connection.id));
  });
  bluetooth.onDisconnected([](const EspBleConnection &) {
    connectionId = 0;
    Serial.println("BLUEDROID_DISCONNECTED");
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (!scanning || connectionId != 0) return;
    if (result.name != String(TARGET_NAME)) return;
    scanning = false;
    bluetooth.scanner().stop();
    Serial.println("BLUEDROID_FOUND");
    Serial.printf("BLUEDROID_CONNECT accepted=%u\n",
      bluetooth.connect(result) ? 1 : 0);
  });

  auto &host = bluetooth.hidHost();
  host.onDiscovered([](const EspBleHidKeyboardHostDiscovery &result) {
    Serial.printf(
      "BLUEDROID_DISCOVERED success=%u report=%u country=%u/%u output=%u "
      "battery=%u/%u detail=[%s]\n",
      result.success ? 1 : 0, result.reportId, result.hasCountryCode ? 1 : 0,
      result.countryCode, result.hasOutputReport ? 1 : 0,
      result.hasBatteryLevel ? 1 : 0, result.batteryLevel,
      result.detail.c_str());
  });
  host.onKeyboard([](const EspBleHidKeyboardEvent &event) { reportKey(event); });
  Serial.println("BLUEDROID_HOST_READY");
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("BLUEDROID_READY");
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (mode == 0 && (command == 'd' || command == 'h'))
    {
      mode = command;
      if (mode == 'd') startDevice();
      else startHost();
    }
    else if (command == '?')
    {
      printState();
    }
    else if (command == '0')
    {
      // Back to no mode, so a test that runs after another one does not inherit
      // the previous role: pytest reflashes only when the binary changed, and a
      // board that was not reflashed was never reset either.
      bluetooth.end();
      mode = 0;
      connectionId = 0;
      scanning = false;
      printState();
    }
    else if (mode == 'h' && command == 's')
    {
      EspBleScanConfig scanConfig;
      scanConfig.active = true;
      scanConfig.durationSeconds = 10;
      scanning = bluetooth.scanner().start(scanConfig);
      Serial.printf("BLUEDROID_SCAN started=%u\n", scanning ? 1 : 0);
    }
    else if (mode == 'h' && command == 'D')
    {
      Serial.printf("BLUEDROID_DISCOVER accepted=%u\n",
        bluetooth.hidHost().discover(connectionId) ? 1 : 0);
    }
    else if (mode == 'h' && command == 'l')
    {
      Serial.printf("BLUEDROID_LEDS accepted=%u\n",
        bluetooth.hidHost().setKeyboardLeds(connectionId, false, true, false)
          ? 1 : 0);
    }
    else if (mode == 'd' && command == 'a')
    {
      EspBleHidKeyboardReport report;
      report.modifiers = EspBleHidKeyboardReport::LeftShift;
      report.keys[0] = 0x04;
      Serial.printf("BLUEDROID_SEND press=%u error=%s\n",
        bluetooth.hidKeyboard().sendReport(report) ? 1 : 0,
        bluetooth.lastErrorName());
    }
    else if (mode == 'd' && command == 'r')
    {
      Serial.printf("BLUEDROID_SEND release=%u error=%s\n",
        bluetooth.hidKeyboard().releaseAll() ? 1 : 0,
        bluetooth.lastErrorName());
    }
  }
  bluetooth.update();
  delay(1);
}
