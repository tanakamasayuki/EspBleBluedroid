// Cross-stack GATT: this library as the Central against an EspBle (NimBLE)
// Peripheral. This is the `peer_device` half of the scenario — the board that
// runs the library under test — while the ESP32-S3 running EspBle is the parent
// fixture, because the S3 is the board that is not always connected.
//
// Bluedroid talking to Bluedroid cannot reveal an implementation that leans on
// Bluedroid's own behaviour, because both ends make the same assumption and it
// cancels out. Here the other end is a different host stack, so what is verified
// is the procedure and the bytes on the air: MTU exchange, discovery,
// characteristic and descriptor Read/Write, and both Notification and
// Indication.
//
// Every step is driven by a serial command instead of chaining off the previous
// callback, so a failure names the step that failed rather than stopping the
// whole sequence at its first surprise.

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Interop UUIDs live in the 01xx suite-tag range (tests/TEST_PLAN.md).
static constexpr const char *SERVICE_UUID =
  "01000000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *CHARACTERISTIC_UUID =
  "01000001-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *DESCRIPTOR_UUID =
  "01000002-b1dd-4d00-9e5a-627564726f69";

static constexpr uint8_t WRITE_VALUE[] = {0x2a, 0x00, 0xfa, 0x5e};
static constexpr uint8_t DESCRIPTOR_WRITE_VALUE[] = {0x2d, 0x00, 0xf9};

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
bool connectionRequested = false;
EspBleConnectionId connectionId = 0;
uint16_t characteristicHandle = 0;
uint16_t descriptorHandle = 0;

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

void printHex(const String &value)
{
  for (size_t index = 0; index < value.length(); ++index)
  {
    Serial.printf("%02x", static_cast<uint8_t>(value[index]));
  }
}

void reportResult(const char *label, const EspBleGattResult &result)
{
  Serial.printf("%s success=%u error=%s length=%u hex=", label,
    result.success ? 1 : 0, errorName(result.error),
    static_cast<unsigned>(result.value.length()));
  printHex(result.value);
  Serial.printf(" context=%s\n", contextName());
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
    Serial.printf("CONNECTED id=%u mtu=%u\n",
      static_cast<unsigned>(connection.id),
      static_cast<unsigned>(connection.mtu));
  });
  bluetooth.onMtuChanged([](const EspBleMtuChanged &event) {
    // Both libraries prefer 247, so a cross-stack link should settle there.
    Serial.printf("MTU previous=%u mtu=%u context=%s\n",
      static_cast<unsigned>(event.previousMtu),
      static_cast<unsigned>(event.connection.mtu), contextName());
  });
  bluetooth.onServicesDiscovered([](const EspBleGattResult &result) {
    characteristicHandle = 0;
    descriptorHandle = 0;
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
    const size_t descriptorCount = bluetooth.discoveredDescriptorCount(
      result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID);
    for (size_t index = 0; index < descriptorCount; ++index)
    {
      EspBleGattDescriptorInfo info;
      if (bluetooth.discoveredDescriptor(result.connectionId, index, info,
            SERVICE_UUID, CHARACTERISTIC_UUID) &&
          info.descriptorUuid.equalsIgnoreCase(DESCRIPTOR_UUID))
      {
        descriptorHandle = info.handle;
      }
    }
    // The properties the peer declared have to survive the trip across stacks:
    // a characteristic that lost its notify bit would fail later and elsewhere.
    bool properties = false;
    for (size_t index = 0;
         index < bluetooth.discoveredCharacteristicCount(result.connectionId);
         ++index)
    {
      EspBleGattCharacteristicInfo info;
      if (bluetooth.discoveredCharacteristic(result.connectionId, index, info) &&
          info.handle == characteristicHandle)
      {
        properties = info.readable && info.writable &&
          info.writableWithoutResponse && info.notifiable && info.indicatable;
      }
    }
    Serial.printf(
      "DISCOVERY success=%u services=%u characteristic=%u descriptor=%u properties=%u context=%s\n",
      result.success ? 1 : 0,
      static_cast<unsigned>(
        bluetooth.discoveredServiceCount(result.connectionId)),
      static_cast<unsigned>(characteristicHandle),
      static_cast<unsigned>(descriptorHandle), properties ? 1 : 0,
      contextName());
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    reportResult("READ", result);
  });
  bluetooth.onCharacteristicWritten([](const EspBleGattResult &result) {
    Serial.printf("WRITE success=%u error=%s response=%u context=%s\n",
      result.success ? 1 : 0, errorName(result.error),
      result.response ? 1 : 0, contextName());
  });
  bluetooth.onDescriptorRead([](const EspBleGattResult &result) {
    reportResult("DESCRIPTOR_READ", result);
  });
  bluetooth.onDescriptorWritten([](const EspBleGattResult &result) {
    Serial.printf("DESCRIPTOR_WRITE success=%u error=%s context=%s\n",
      result.success ? 1 : 0, errorName(result.error), contextName());
  });
  bluetooth.onSubscribed([](const EspBleGattResult &result) {
    Serial.printf("SUBSCRIBED success=%u notifications=%u indications=%u context=%s\n",
      result.success ? 1 : 0, result.subscribedToNotifications ? 1 : 0,
      result.subscribedToIndications ? 1 : 0, contextName());
  });
  bluetooth.onUnsubscribed([](const EspBleGattResult &result) {
    Serial.printf("UNSUBSCRIBED success=%u context=%s\n",
      result.success ? 1 : 0, contextName());
  });
  bluetooth.onNotification([](const EspBleGattNotification &notification) {
    Serial.printf("NOTIFICATION indication=%u handle=%u length=%u hex=",
      notification.indication ? 1 : 0,
      static_cast<unsigned>(notification.handle),
      static_cast<unsigned>(notification.value.length()));
    printHex(notification.value);
    Serial.printf(" context=%s\n", contextName());
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

  Serial.println("INTEROP_GATT_BASIC_READY");
}

void loop()
{
  bluetooth.update();
  if (!Serial.available())
  {
    delay(1);
    return;
  }
  switch (Serial.read())
  {
    case 'c':
    {
      EspBleScanConfig scanConfig;
      scanConfig.active = true;
      Serial.printf("SCAN_STARTED %u\n",
        bluetooth.scanner().start(scanConfig) ? 1 : 0);
      break;
    }
    case 'd':
      Serial.printf("DISCOVERY_REQUESTED %u\n",
        bluetooth.discoverServices(connectionId, 8000) ? 1 : 0);
      break;
    case 'r':
      Serial.printf("READ_REQUESTED %u\n",
        bluetooth.readCharacteristic(connectionId, characteristicHandle, 5000)
          ? 1 : 0);
      break;
    case 'w':
      Serial.printf("WRITE_REQUESTED %u\n",
        bluetooth.writeCharacteristic(connectionId, characteristicHandle,
          WRITE_VALUE, sizeof(WRITE_VALUE), true, 5000) ? 1 : 0);
      break;
    case 'W':
      Serial.printf("WRITE_NO_RESPONSE_REQUESTED %u\n",
        bluetooth.writeCharacteristic(connectionId, characteristicHandle,
          WRITE_VALUE, sizeof(WRITE_VALUE), false, 5000) ? 1 : 0);
      break;
    case 'e':
      Serial.printf("DESCRIPTOR_READ_REQUESTED %u\n",
        bluetooth.readDescriptor(connectionId, descriptorHandle, 5000) ? 1 : 0);
      break;
    case 'f':
      Serial.printf("DESCRIPTOR_WRITE_REQUESTED %u\n",
        bluetooth.writeDescriptor(connectionId, descriptorHandle,
          DESCRIPTOR_WRITE_VALUE, sizeof(DESCRIPTOR_WRITE_VALUE), true, 5000)
          ? 1 : 0);
      break;
    case 's':
      Serial.printf("SUBSCRIBE_REQUESTED %u\n",
        bluetooth.subscribe(connectionId, characteristicHandle, true, 5000)
          ? 1 : 0);
      break;
    case 'S':
      // Indications: the same CCCD, the other bit.
      Serial.printf("SUBSCRIBE_INDICATIONS_REQUESTED %u\n",
        bluetooth.subscribe(connectionId, characteristicHandle, false, 5000)
          ? 1 : 0);
      break;
    case 'u':
      Serial.printf("UNSUBSCRIBE_REQUESTED %u\n",
        bluetooth.unsubscribe(connectionId, characteristicHandle, 5000) ? 1 : 0);
      break;
    case 'x':
      Serial.printf("DISCONNECT_REQUESTED %u\n",
        bluetooth.disconnect(connectionId) ? 1 : 0);
      break;
    default:
      break;
  }
  delay(1);
}
