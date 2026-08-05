// The EspBle (NimBLE) half of the cross-stack HID over GATT scenario, running on an
// ESP32-S3 against the released EspBle pinned in sketch.yaml.
//
// The Report Descriptors are already known to be byte-identical
// (tests/unit/hid_report_maps), and both sides parse them with the same helper
// (tests/unit/report_map). What no diff can show is whether a *different
// implementation* reaches the same conclusions from those bytes on the air: that
// every Report characteristic sharing UUID 0x2A4D can be told apart by its Report
// Reference, that a keystroke decodes to the same usage and character, and that the
// LED write goes back the other way. That is what this suite exercises, with each
// side using its own library for both encoding and decoding.
//
// One firmware serves both directions. The mode command chooses whether this board
// is the HID Device (peripheral) or the HID Host (central), and the role is part of
// the advertised name so the other side never latches onto the wrong one: the HID
// UUIDs are fixed by the specification, so a suite-tag UUID cannot be used for
// isolation here (tests/TEST_PLAN.md).
//
// Output is prefixed ESPBLE_ so a log line never leaves it ambiguous which stack
// produced it.

#include <EspBle.h>

static constexpr const char *DEVICE_NAME = "EspBle HID Device 0107";
static constexpr const char *TARGET_NAME = "Bluedroid HID Device 0107";
// Deliberately non-default, so the other stack's discovery result has to have been
// read from these attributes rather than assumed.
static constexpr uint8_t COUNTRY_CODE = 33;
static constexpr uint8_t BATTERY_LEVEL = 73;

EspBle ble;
char mode = 0;
EspBleConnectionId connectionId = 0;
bool scanning = false;

void reportKey(const EspBleHidKeyboardEvent &event)
{
  Serial.printf(
    "ESPBLE_KEY usage=%u ascii=%u pressed=%u released=%u mods=0x%02x length=%u\n",
    event.usage, event.ascii, event.pressed ? 1 : 0, event.released ? 1 : 0,
    event.modifiers, static_cast<unsigned>(event.rawLength));
}

void printState()
{
  Serial.printf("ESPBLE_STATE mode=%c id=%u ready=%u\n",
    mode == 0 ? '-' : mode, static_cast<unsigned>(connectionId),
    mode == 'd' ? (ble.hidKeyboard().ready() ? 1 : 0)
                : (ble.hidHost().ready(connectionId) ? 1 : 0));
}

void startDevice()
{
  auto &keyboard = ble.hidKeyboard();
  EspBleHidKeyboardConfig keyboardConfig;
  keyboardConfig.manufacturer = "EspBle Interop";
  keyboardConfig.countryCode = COUNTRY_CODE;
  keyboardConfig.initialBatteryLevel = BATTERY_LEVEL;
  if (!keyboard.configure(keyboardConfig))
  {
    Serial.printf("ESPBLE_CONFIG_FAILED %s\n", ble.lastErrorName());
    return;
  }
  keyboard.onOutputReport([](const EspBleHidKeyboardOutputReport &report) {
    Serial.printf("ESPBLE_LED leds=0x%02x caps=%u\n", report.leds,
      report.capsLock ? 1 : 0);
  });

  EspBleConfig config;
  config.deviceName = DEVICE_NAME;
  if (!ble.begin(config))
  {
    Serial.printf("ESPBLE_BEGIN_FAILED %s\n", ble.lastErrorName());
    return;
  }
  ble.onDisconnected([](const EspBleConnection &) { ble.advertising().start(); });
  ble.advertising().setName(DEVICE_NAME);
  if (!ble.advertising().start())
  {
    Serial.printf("ESPBLE_ADVERTISE_FAILED %s\n", ble.lastErrorName());
    return;
  }
  Serial.println("ESPBLE_DEVICE_READY");
}

void startHost()
{
  EspBleConfig config;
  config.deviceName = "EspBle HID Host 0107";
  if (!ble.begin(config))
  {
    Serial.printf("ESPBLE_BEGIN_FAILED %s\n", ble.lastErrorName());
    return;
  }
  ble.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    Serial.printf("ESPBLE_CONNECTED id=%u\n",
      static_cast<unsigned>(connection.id));
  });
  ble.onDisconnected([](const EspBleConnection &) {
    connectionId = 0;
    Serial.println("ESPBLE_DISCONNECTED");
  });
  ble.scanner().onResult([](const EspBleScanResult &result) {
    if (!scanning || connectionId != 0) return;
    if (result.name != String(TARGET_NAME)) return;
    scanning = false;
    ble.scanner().stop();
    Serial.println("ESPBLE_FOUND");
    Serial.printf("ESPBLE_CONNECT accepted=%u\n", ble.connect(result) ? 1 : 0);
  });

  auto &host = ble.hidHost();
  host.onDiscovered([](const EspBleHidKeyboardHostDiscovery &result) {
    Serial.printf(
      "ESPBLE_DISCOVERED success=%u report=%u country=%u/%u output=%u "
      "battery=%u/%u\n",
      result.success ? 1 : 0, result.reportId, result.hasCountryCode ? 1 : 0,
      result.countryCode, result.hasOutputReport ? 1 : 0,
      result.hasBatteryLevel ? 1 : 0, result.batteryLevel);
  });
  host.onKeyboard([](const EspBleHidKeyboardEvent &event) { reportKey(event); });
  Serial.println("ESPBLE_HOST_READY");
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("ESPBLE_READY");
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
      ble.end();
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
      scanning = ble.scanner().start(scanConfig);
      Serial.printf("ESPBLE_SCAN started=%u\n", scanning ? 1 : 0);
    }
    else if (mode == 'h' && command == 'D')
    {
      Serial.printf("ESPBLE_DISCOVER accepted=%u\n",
        ble.hidHost().discover(connectionId) ? 1 : 0);
    }
    else if (mode == 'h' && command == 'l')
    {
      Serial.printf("ESPBLE_LEDS accepted=%u\n",
        ble.hidHost().setKeyboardLeds(connectionId, false, true, false) ? 1 : 0);
    }
    else if (mode == 'd' && command == 'a')
    {
      // Shift+A: the other stack has to decode usage 0x04 with the shift modifier
      // into the character 'A' through its own layout table.
      EspBleHidKeyboardReport report;
      report.modifiers = EspBleHidKeyboardReport::LeftShift;
      report.keys[0] = 0x04;
      Serial.printf("ESPBLE_SEND press=%u error=%s\n",
        ble.hidKeyboard().sendReport(report) ? 1 : 0, ble.lastErrorName());
    }
    else if (mode == 'd' && command == 'r')
    {
      Serial.printf("ESPBLE_SEND release=%u error=%s\n",
        ble.hidKeyboard().releaseAll() ? 1 : 0, ble.lastErrorName());
    }
  }
  ble.update();
  delay(1);
}
