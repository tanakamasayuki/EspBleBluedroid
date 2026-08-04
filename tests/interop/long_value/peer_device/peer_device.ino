// Cross-stack long value: this library's GATT client reading a Characteristic
// whose value does not fit in one ATT response, published by an EspBle (NimBLE)
// peripheral. This is the `peer_device` half — the board running the library
// under test — while the ESP32-S3 running EspBle is the parent fixture.
//
// Bluedroid's client API has no Read Blob / Read Long entry point
// (esp_gattc_api.h offers read_char, read_by_type, read_multiple and
// read_char_descr only), so a truncated read at mtu - 1 would be an easy
// assumption to make; this repository's documentation made it until
// `peer/long_value` ran. Bluedroid continues the read internally instead. That
// scenario has Bluedroid on both ends, though, so the continuation could just as
// well be two halves of the same stack agreeing. Here the responder is NimBLE.
//
// Both public entry points are exercised — the UUID form and the attribute-handle
// form — because they take different paths inside the library, and every byte is
// compared against the peer's ramp so a value reassembled out of order cannot
// pass as a complete one.

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Interop UUIDs live in the 01xx suite-tag range (tests/TEST_PLAN.md).
static constexpr const char *SERVICE_UUID =
  "01020000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *CHARACTERISTIC_UUID =
  "01020001-b1dd-4d00-9e5a-627564726f69";

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
bool connectionRequested = false;
EspBleConnectionId connectionId = 0;
uint16_t characteristicHandle = 0;
uint16_t negotiatedMtu = 23;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

const char *errorName(EspBleError error)
{
  switch (error)
  {
    case EspBleError::None: return "None";
    case EspBleError::InvalidState: return "InvalidState";
    case EspBleError::InvalidArgument: return "InvalidArgument";
    case EspBleError::BackendFailure: return "BackendFailure";
    case EspBleError::NotFound: return "NotFound";
    case EspBleError::Timeout: return "Timeout";
    default: return "Other";
  }
}

// The peer fills its value with byte i = i & 0xff — the same ramp
// `peer/long_value` uses, per the shared-expectations rule in tests/TEST_PLAN.md.
// Checking every byte catches a value reassembled out of order, which a length
// check alone would pass.
bool isRamp(const String &value)
{
  for (size_t index = 0; index < value.length(); ++index)
  {
    if (static_cast<uint8_t>(value[index]) !=
        static_cast<uint8_t>(index & 0xff))
    {
      return false;
    }
  }
  return true;
}

void reportRead(const char *label, const EspBleGattResult &result)
{
  Serial.printf("%s success=%u error=%s length=%u mtu=%u ramp=%u context=%s\n",
    label, result.success ? 1 : 0, errorName(result.error),
    static_cast<unsigned>(result.value.length()),
    static_cast<unsigned>(negotiatedMtu),
    isRamp(result.value) ? 1 : 0, contextName());
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();

  if (!bluetooth.begin())
  {
    Serial.printf("INIT_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    negotiatedMtu = connection.mtu;
    Serial.printf("CONNECTED id=%u mtu=%u\n",
      static_cast<unsigned>(connection.id),
      static_cast<unsigned>(connection.mtu));
  });
  bluetooth.onMtuChanged([](const EspBleMtuChanged &event) {
    negotiatedMtu = event.connection.mtu;
    Serial.printf("MTU previous=%u mtu=%u context=%s\n",
      static_cast<unsigned>(event.previousMtu),
      static_cast<unsigned>(event.connection.mtu), contextName());
  });
  bluetooth.onServicesDiscovered([](const EspBleGattResult &result) {
    characteristicHandle = 0;
    for (size_t index = 0;
         index < bluetooth.discoveredCharacteristicCount(result.connectionId);
         ++index)
    {
      EspBleGattCharacteristicInfo info;
      if (bluetooth.discoveredCharacteristic(result.connectionId, index, info) &&
          info.serviceUuid.equalsIgnoreCase(SERVICE_UUID) &&
          info.characteristicUuid.equalsIgnoreCase(CHARACTERISTIC_UUID))
      {
        characteristicHandle = info.handle;
      }
    }
    Serial.printf("DISCOVERY success=%u characteristic=%u context=%s\n",
      result.success ? 1 : 0, static_cast<unsigned>(characteristicHandle),
      contextName());
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    reportRead("READ", result);
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf("DISCONNECTED id=%u dropped=%u context=%s\n",
      static_cast<unsigned>(connection.id),
      static_cast<unsigned>(bluetooth.droppedEventCount()), contextName());
  });
  bluetooth.onConnectionFailed([](const EspBleConnectionFailure &failure) {
    Serial.printf("CONNECT_FAILED detail=%s\n", failure.detail.c_str());
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested || !result.advertisesService(SERVICE_UUID)) return;
    connectionRequested = true;
    bluetooth.scanner().stop();
    Serial.printf("TARGET_FOUND address=%s name=%s\n",
      result.address.c_str(), result.name.c_str());
    Serial.printf("CONNECT_REQUESTED %u\n",
      bluetooth.connect(result, 10000) ? 1 : 0);
  });

  Serial.println("INTEROP_LONG_VALUE_READY");
}

// Every step is driven by a serial command instead of chaining off the previous
// callback, so a failure names the step that failed rather than stopping the
// whole sequence at its first surprise.
void loop()
{
  bluetooth.update();
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == 'c')
    {
      EspBleScanConfig scanConfig;
      scanConfig.active = true;
      Serial.printf("SCAN_STARTED %u\n",
        bluetooth.scanner().start(scanConfig) ? 1 : 0);
    }
    else if (command == 'd')
    {
      Serial.printf("DISCOVERY_REQUESTED %u\n",
        bluetooth.discoverServices(connectionId, 5000) ? 1 : 0);
    }
    else if (command == 'r')
    {
      Serial.printf("UUID_READ_REQUESTED %u\n",
        bluetooth.readCharacteristic(
          connectionId, SERVICE_UUID, CHARACTERISTIC_UUID, 5000) ? 1 : 0);
    }
    else if (command == 'R')
    {
      Serial.printf("HANDLE_READ_REQUESTED %u\n",
        bluetooth.readCharacteristic(connectionId, characteristicHandle, 5000)
          ? 1 : 0);
    }
    else if (command == 'x')
    {
      Serial.printf("DISCONNECT_REQUESTED %u\n",
        bluetooth.disconnect(connectionId) ? 1 : 0);
    }
  }
  delay(1);
}
