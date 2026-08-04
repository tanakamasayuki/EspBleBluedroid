// Two Characteristics with one UUID inside one Service, published by this
// library's GATT Server and addressed from the outside.
//
// `peer/duplicate_uuid` covers the registration contract (accepted, each with its
// own handle) and the client half (a peer's duplicates reached by handle). What
// neither shows is whether the *published database* really carries two attributes:
// a backend that reused the first entry would still hand out two handles at
// registration and still let every local call succeed. Only a peer reading both
// can tell, which is what this suite does — and it is the prerequisite HID over
// GATT rests on, because a keyboard's Report characteristics all carry 0x2a4d.
//
// The pair is deliberately shaped like HID: each Characteristic carries its own
// Report-Reference-style Descriptor with a different value, so "which one am I
// talking to" has an answer at every level — value, descriptor, write, CCCD and
// notification.

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *SERVICE_UUID =
  "000b0000-b1dd-4d00-9e5a-627564726f69";
// One UUID, two characteristics.
static constexpr const char *CHARACTERISTIC_UUID =
  "000b0001-b1dd-4d00-9e5a-627564726f69";
// One descriptor UUID under each of them: legal, because the restriction is per
// characteristic (`peer/duplicate_uuid` pins that half).
static constexpr const char *DESCRIPTOR_UUID =
  "000b0002-b1dd-4d00-9e5a-627564726f69";

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
bool started = false;

EspBleGattCharacteristic first;
EspBleGattCharacteristic second;
EspBleGattDescriptor firstDescriptor;
EspBleGattDescriptor secondDescriptor;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();

  auto &server = bluetooth.gattServer();
  EspBleGattCharacteristicConfig config;
  config.readable = true;
  config.writable = true;
  config.notifiable = true;
  EspBleGattDescriptorConfig descriptorConfig;

  const EspBleGattService service = server.addService(SERVICE_UUID);
  first = server.addCharacteristic(service, CHARACTERISTIC_UUID, config);
  second = server.addCharacteristic(service, CHARACTERISTIC_UUID, config);
  firstDescriptor = server.addDescriptor(first, DESCRIPTOR_UUID, descriptorConfig);
  secondDescriptor =
    server.addDescriptor(second, DESCRIPTOR_UUID, descriptorConfig);

  const bool values =
    server.setValue(first, String("first-value")) &&
    server.setValue(second, String("second-value")) &&
    server.setDescriptorValue(firstDescriptor, String("report-a")) &&
    server.setDescriptorValue(secondDescriptor, String("report-b"));
  if (!service || !first || !second || !firstDescriptor || !secondDescriptor ||
      !values)
  {
    Serial.printf("CONFIG_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }

  // Every server event carries the handle, which is the only thing that can tell
  // the two apart — the UUID cannot.
  server.onWritten([](const EspBleGattWrite &write) {
    Serial.printf("WRITE id=%u value=%s context=%s\n",
      static_cast<unsigned>(write.characteristic.id), write.value.c_str(),
      contextName());
  });
  server.onSubscriptionChanged([](const EspBleGattSubscription &subscription) {
    Serial.printf("SUBSCRIPTION id=%u notifications=%u context=%s\n",
      static_cast<unsigned>(subscription.characteristic.id),
      subscription.notifications ? 1 : 0, contextName());
  });
  server.onSent([](const EspBleGattSendResult &result) {
    Serial.printf("SENT id=%u success=%u context=%s\n",
      static_cast<unsigned>(result.characteristic.id), result.success ? 1 : 0,
      contextName());
  });

  EspBleConfig bleConfig;
  bleConfig.deviceName = "Bluedroid Duplicate Server";
  if (!bluetooth.begin(bleConfig))
  {
    Serial.printf("BEGIN_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }
  bluetooth.advertising().addServiceUuid(SERVICE_UUID);
  if (!bluetooth.advertising().start())
  {
    Serial.printf("ADVERTISE_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  started = true;
  Serial.println("DUPLICATE_SERVER_READY");
}

void loop()
{
  bluetooth.update();
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == '?')
    {
      // Requested rather than printed at boot: the first output is lost while the
      // other board is flashed.
      Serial.printf("READY_STATE started=%u\n", started ? 1 : 0);
    }
    else if (command == 'r')
    {
      Serial.printf("REGISTRATION accepted=%u distinct=%u descriptors=%u\n",
        (first.valid() ? 1u : 0u) + (second.valid() ? 1u : 0u),
        first.valid() && second.valid() && first != second ? 1 : 0,
        (firstDescriptor.valid() ? 1u : 0u) + (secondDescriptor.valid() ? 1u : 0u));
    }
    else if (command == '1')
    {
      Serial.printf("NOTIFY_ACCEPTED id=1 %u\n",
        bluetooth.gattServer().notify(first, String("notify-a")) ? 1 : 0);
    }
    else if (command == '2')
    {
      Serial.printf("NOTIFY_ACCEPTED id=2 %u\n",
        bluetooth.gattServer().notify(second, String("notify-b")) ? 1 : 0);
    }
  }
  delay(1);
}
