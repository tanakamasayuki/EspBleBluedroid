// Pins what a Read returns for a Characteristic value that does not fit in one
// ATT response.
//
// Bluedroid's public GATT client API has no Read Blob / Read Long entry point
// (esp_gattc_api.h offers read_char, read_by_type, read_multiple and
// read_char_descr only), so it would be easy to assume the value comes back
// truncated to mtu - 1 bytes. It does not: this test showed that Bluedroid
// continues the read internally and hands the whole value up, matching what
// EspBle does over NimBLE. The scenario exists to keep that agreement, because
// nothing in the API surface promises it and a value silently cut short is the
// kind of failure an application discovers in the field.
//
// Both public entry points are checked — the UUID form and the attribute-handle
// form — because they take different paths inside the library, and every byte is
// compared against a known ramp so a rearranged value cannot pass as a complete
// one.

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *SERVICE_UUID =
  "00040000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *CHARACTERISTIC_UUID =
  "00040001-b1dd-4d00-9e5a-627564726f69";

// What the peer publishes: PEER_VALUE_LENGTH bytes of a known ramp, which is
// longer than any legal ATT payload on this link.
static constexpr size_t PEER_VALUE_LENGTH = 300;

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
bool connectionRequested = false;
// A scan can legitimately miss every advertisement for a while, and this scenario
// needs exactly one result to get going. Restarting the scan is what any
// application would do, and it keeps a single miss from failing a test that is
// about long reads (tests/TEST_PLAN.md, known intermittent failures). A restart
// that does not help still fails the test: the defect it would hide does not exist
// while the restarts keep failing too.
static constexpr uint32_t ScanRetryMilliseconds = 12000;
static constexpr uint8_t MaxScanRetries = 2;
uint32_t scanStartedAt = 0;
uint8_t scanRetries = 0;
uint16_t characteristicHandle = 0;
uint16_t negotiatedMtu = 23;
uint8_t readPhase = 0;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

// The peer fills its value with byte i = i & 0xff. Checking every byte against
// that ramp catches a value that was reassembled out of order, which a length
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
  Serial.printf(
    "%s success=%u length=%u mtu=%u ramp=%u context=%s\n",
    label, result.success ? 1 : 0,
    static_cast<unsigned>(result.value.length()),
    static_cast<unsigned>(negotiatedMtu),
    isRamp(result.value) ? 1 : 0, contextName());
}

void startScan()
{
  EspBleScanConfig scanConfig;
  scanConfig.active = true;
  bluetooth.scanner().start(scanConfig);
  scanStartedAt = millis();
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

  bluetooth.onMtuChanged([](const EspBleMtuChanged &event) {
    negotiatedMtu = event.connection.mtu;
    Serial.printf("MTU mtu=%u context=%s\n",
      static_cast<unsigned>(negotiatedMtu), contextName());
    const bool accepted = bluetooth.discoverServices(event.connection.id, 5000);
    Serial.printf("DISCOVERY_REQUESTED %u\n", accepted ? 1 : 0);
  });
  bluetooth.onConnected([](const EspBleConnection &connection) {
    // Reading is driven from the MTU event, not from here: at the initial 23 the
    // read would need many more continuations, so the interesting case is the
    // negotiated link.
    negotiatedMtu = connection.mtu;
    Serial.printf("CONNECTED id=%u mtu=%u\n",
      static_cast<unsigned>(connection.id),
      static_cast<unsigned>(connection.mtu));
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
    Serial.printf("DISCOVERY success=%u handle=%u\n",
      result.success ? 1 : 0, static_cast<unsigned>(characteristicHandle));
    const bool accepted = bluetooth.readCharacteristic(
      result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID, 5000);
    Serial.printf("UUID_READ_REQUESTED %u\n", accepted ? 1 : 0);
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    if (readPhase++ == 0)
    {
      reportRead("UUID_READ", result);
      const bool accepted = bluetooth.readCharacteristic(
        result.connectionId, characteristicHandle, 5000);
      Serial.printf("HANDLE_READ_REQUESTED %u\n", accepted ? 1 : 0);
      return;
    }
    reportRead("HANDLE_READ", result);
    Serial.printf("PEER_VALUE_LENGTH %u\n",
      static_cast<unsigned>(PEER_VALUE_LENGTH));
    bluetooth.disconnect(result.connectionId);
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf("DISCONNECTED id=%u dropped=%u context=%s\n",
      static_cast<unsigned>(connection.id),
      static_cast<unsigned>(bluetooth.droppedEventCount()), contextName());
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested || !result.advertisesService(SERVICE_UUID)) return;
    connectionRequested = true;
    bluetooth.scanner().stop();
    Serial.printf("TARGET_FOUND %s\n", result.address.c_str());
    Serial.printf("CONNECT_REQUESTED %u\n",
      bluetooth.connect(result, 10000) ? 1 : 0);
  });

  startScan();
  Serial.println("LONG_VALUE_READY");
}

void loop()
{
  if (!connectionRequested && scanRetries < MaxScanRetries &&
      static_cast<uint32_t>(millis() - scanStartedAt) > ScanRetryMilliseconds)
  {
    ++scanRetries;
    bluetooth.scanner().stop();
    startScan();
    Serial.printf("SCAN_RESTARTED %u\n", scanRetries);
  }
  bluetooth.update();
  delay(1);
}
