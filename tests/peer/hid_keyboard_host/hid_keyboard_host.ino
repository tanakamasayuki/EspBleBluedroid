// The HID Host side of HOGP: this sketch is the central, and it consumes a HID
// keyboard's reports rather than producing them.
//
// A host cannot assume a layout, so `discover()` reads the peer's Report Map, reads
// each Report Reference to learn which 0x2A4D attribute carries which report, and
// subscribes to the Input Reports. This backend allows one central GATT operation
// at a time, so all of that is a sequence driven by results — `?` reports whether
// it has finished, and every step's outcome is printed.
//
// The peer is this library's own keyboard device (peer/hid_keyboard_device already
// pins that device against a raw central, and interop/hid will cross-check both
// sides against EspBle).
//
// The HID UUIDs are fixed by the specification, so isolation is by device name.

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *TARGET_NAME = "Bluedroid HID 0010";

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
EspBleConnectionId connectionId = 0;
bool scanning = false;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();

  EspBleConfig config;
  config.deviceName = "Bluedroid HID Host 0010";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BEGIN_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    Serial.printf("CONNECTED id=%u context=%s\n",
      static_cast<unsigned>(connection.id), contextName());
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf("DISCONNECTED id=%u\n", static_cast<unsigned>(connection.id));
    connectionId = 0;
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (result.name != String(TARGET_NAME)) return;
    if (connectionId != 0 || !scanning) return;
    scanning = false;
    bluetooth.scanner().stop();
    Serial.printf("FOUND rssi=%d\n", result.rssi);
    // connect(scanResult) takes the address and its type from the result itself.
    if (!bluetooth.connect(result))
    {
      Serial.printf("CONNECT_FAILED %s\n", bluetooth.lastErrorName());
    }
  });

  auto &host = bluetooth.hidHost();
  host.onDiscovered([](const EspBleHidKeyboardHostDiscovery &result) {
    // What the device turned out to be, read from its own attributes rather than
    // assumed: the keyboard's report ID, the country code from HID Information,
    // whether it has an Output Report, and the battery level.
    Serial.printf(
      "DISCOVERED success=%u id=%u report=%u country=%u/%u output=%u "
      "battery=%u/%u detail=[%s] context=%s\n",
      result.success ? 1 : 0, static_cast<unsigned>(result.connectionId),
      result.reportId, result.hasCountryCode ? 1 : 0, result.countryCode,
      result.hasOutputReport ? 1 : 0, result.hasBatteryLevel ? 1 : 0,
      // The detail names the step that failed, which lastError() cannot: the
      // discovery sequence is asynchronous, so lastError() has moved on by now.
      result.batteryLevel, result.detail.c_str(), contextName());
  });
  host.onKeyboard([](const EspBleHidKeyboardEvent &event) {
    Serial.printf(
      "KEY usage=%u ascii=%u pressed=%u released=%u mods=0x%02x caps=%u "
      "length=%u context=%s\n",
      event.usage, event.ascii, event.pressed ? 1 : 0, event.released ? 1 : 0,
      event.modifiers, event.capsLock ? 1 : 0,
      static_cast<unsigned>(event.rawLength), contextName());
  });
  host.onKeyboardState([](const EspBleHidKeyboardState &state) {
    // The whole state, as the usages that are down: a host that tracks chords
    // needs the state rather than the edges.
    Serial.printf("STATE mods=0x%02x down=", state.modifiers);
    unsigned count = 0;
    for (uint16_t usage = 0; usage < 8 * EspBleHidKeyboardState::BitmapSize; ++usage)
    {
      if (!state.isDown(static_cast<uint8_t>(usage))) continue;
      Serial.printf("%s%u", count++ == 0 ? "" : ",", usage);
    }
    if (count == 0) Serial.print("none");
    Serial.printf(" count=%u\n", count);
  });

  Serial.println("HID_HOST_READY");
}

void loop()
{
  bluetooth.update();
  if (Serial.available())
  {
    const char command = Serial.read();
    auto &host = bluetooth.hidHost();
    if (command == '?')
    {
      Serial.printf("READY_STATE id=%u ready=%u invalid=%u\n",
        static_cast<unsigned>(connectionId),
        host.ready(connectionId) ? 1 : 0,
        static_cast<unsigned>(host.invalidInputReportCount()));
    }
    else if (command == 's')
    {
      EspBleScanConfig scanConfig;
      scanConfig.active = true;
      scanConfig.durationSeconds = 10;
      scanning = bluetooth.scanner().start(scanConfig);
      Serial.printf("SCAN started=%u\n", scanning ? 1 : 0);
    }
    else if (command == 'd')
    {
      Serial.printf("DISCOVER accepted=%u error=%s\n",
        host.discover(connectionId) ? 1 : 0, bluetooth.lastErrorName());
    }
    else if (command == 'l')
    {
      // Caps Lock on: the one report a keyboard host writes.
      Serial.printf("LEDS accepted=%u error=%s\n",
        host.setKeyboardLeds(connectionId, false, true, false) ? 1 : 0,
        bluetooth.lastErrorName());
    }
    else if (command == 'L')
    {
      Serial.printf("LEDS accepted=%u error=%s\n",
        host.setKeyboardLeds(connectionId, false, false, false) ? 1 : 0,
        bluetooth.lastErrorName());
    }
    else if (command == 'e')
    {
      // Discovery on a connection that is not there, and an LED write before any
      // discovery finished: the two refusals a caller has to tell apart.
      Serial.printf("DISCOVER_UNKNOWN accepted=%u error=%s\n",
        host.discover(0xfe) ? 1 : 0, bluetooth.lastErrorName());
      Serial.printf("LEDS_UNKNOWN accepted=%u error=%s\n",
        host.setKeyboardLeds(0xfe, true, true, true) ? 1 : 0,
        bluetooth.lastErrorName());
    }
    else if (command == 'x')
    {
      Serial.printf("DISCONNECT accepted=%u\n",
        bluetooth.disconnect(connectionId) ? 1 : 0);
    }
  }
  delay(1);
}
