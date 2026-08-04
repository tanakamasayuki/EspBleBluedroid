// Multi-observer dispatch: the primary on*() callback plus add*Listener().
//
// Until this existed, one event had exactly one owner, so a profile helper (the
// BLE MIDI and HID helpers are the reason it matters) could only work by taking
// the slot the application wanted. The contract verified here is the one EspBle
// documents, because a helper written against either library has to behave the
// same:
//
//   * the primary runs first, then the listeners in registration order;
//   * a list holds four listeners besides the primary, and the fifth is refused
//     with EspBleInvalidListenerId rather than silently dropped;
//   * ids are unique across every list on one object, so removing needs no hint
//     about which event an id belongs to — and removing from the wrong family
//     fails;
//   * a listener added from inside a dispatch is not invoked in that same
//     dispatch, and one removed during it is not invoked afterwards.
//
// The radio half runs in the peripheral role: a raw Arduino-ESP32 central
// connects, subscribes, writes and receives a notification, which is what makes
// the connection lists, the GATT Server lists and the send-completion list fire
// for real.

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *SERVICE_UUID =
  "00080000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *CHARACTERISTIC_UUID =
  "00080001-b1dd-4d00-9e5a-627564726f69";

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
EspBleGattCharacteristic characteristic;

// Every observer appends its own tag, so the assertion is the exact order rather
// than "they all ran".
String connectedOrder;
String writtenOrder;
String subscriptionOrder;
String sentOrder;

// The registration contract, captured at setup and reported on request: the first
// serial output after flashing can be lost while the port reopens.
struct RegistrationReport
{
  EspBleListenerId first = EspBleInvalidListenerId;
  EspBleListenerId second = EspBleInvalidListenerId;
  size_t accepted = 0;
  bool fifthRefused = false;
  bool emptyRefused = false;
  bool removedSecond = false;
  bool removeUnknownFailed = false;
  bool wrongFamilyFailed = false;
  bool rightFamilySucceeded = false;
  bool serverIdIndependent = false;
} registration;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void appendTag(String &order, const char *tag)
{
  if (order.length() > 0) order += ",";
  order += tag;
}

void registerListeners()
{
  // The primary keeps the plain on*() form, so existing code is unaffected.
  bluetooth.onConnected([](const EspBleConnection &) {
    appendTag(connectedOrder, "primary");
  });
  registration.first = bluetooth.addConnectedListener(
    [](const EspBleConnection &) { appendTag(connectedOrder, "L1"); });
  registration.second = bluetooth.addConnectedListener(
    [](const EspBleConnection &) { appendTag(connectedOrder, "L2"); });
  // A listener registered from inside a dispatch must not run in that dispatch:
  // otherwise a helper that installs another observer would see one event twice.
  bluetooth.addConnectedListener([](const EspBleConnection &) {
    appendTag(connectedOrder, "L3");
    bluetooth.addConnectedListener(
      [](const EspBleConnection &) { appendTag(connectedOrder, "late"); });
  });

  // Four listeners fit besides the primary; the fifth is refused.
  EspBleListenerId fourth = bluetooth.addConnectedListener(
    [](const EspBleConnection &) { appendTag(connectedOrder, "L4"); });
  registration.accepted =
    (registration.first != EspBleInvalidListenerId ? 1u : 0u) +
    (registration.second != EspBleInvalidListenerId ? 1u : 0u) +
    (fourth != EspBleInvalidListenerId ? 1u : 0u);
  registration.fifthRefused =
    bluetooth.addConnectedListener([](const EspBleConnection &) {
      appendTag(connectedOrder, "L5");
    }) == EspBleInvalidListenerId;
  registration.emptyRefused =
    bluetooth.addDisconnectedListener(nullptr) == EspBleInvalidListenerId;

  // L2 is removed again, so the order below is primary,L1,L3,L4 — a removal that
  // did nothing would show up as an extra tag.
  registration.removedSecond =
    bluetooth.removeConnectionListener(registration.second);
  registration.removeUnknownFailed =
    !bluetooth.removeConnectionListener(registration.first + 1000);

  // Ids are unique across the lists of one object, and each remove function only
  // owns its own family.
  const EspBleListenerId notification = bluetooth.addNotificationListener(
    [](const EspBleGattNotification &) {});
  registration.wrongFamilyFailed =
    !bluetooth.removeConnectionListener(notification);
  registration.rightFamilySucceeded = bluetooth.removeGattListener(notification);

  auto &server = bluetooth.gattServer();
  server.onWritten([](const EspBleGattWrite &write) {
    appendTag(writtenOrder, "primary");
    Serial.printf("WRITE id=%u value=%s context=%s\n",
      static_cast<unsigned>(write.connectionId), write.value.c_str(),
      contextName());
  });
  server.addWrittenListener(
    [](const EspBleGattWrite &) { appendTag(writtenOrder, "L1"); });
  server.addWrittenListener(
    [](const EspBleGattWrite &) { appendTag(writtenOrder, "L2"); });
  server.onSubscriptionChanged([](const EspBleGattSubscription &subscription) {
    appendTag(subscriptionOrder, "primary");
    Serial.printf("SUBSCRIPTION notifications=%u context=%s\n",
      subscription.notifications ? 1 : 0, contextName());
  });
  server.addSubscriptionChangedListener(
    [](const EspBleGattSubscription &) { appendTag(subscriptionOrder, "L1"); });
  server.onSent([](const EspBleGattSendResult &result) {
    appendTag(sentOrder, "primary");
    Serial.printf("SENT success=%u context=%s\n", result.success ? 1 : 0,
      contextName());
  });
  const EspBleListenerId sent = server.addSentListener(
    [](const EspBleGattSendResult &) { appendTag(sentOrder, "L1"); });
  // Ids are unique per *owner*, not globally: the Server allocates from its own
  // counter, so an id from there only means anything to the Server's
  // removeListener(). Passing one to the connection or GATT-client remover is a
  // programming error that can hit an unrelated listener, exactly as in EspBle —
  // which is why each owner's ids stay with that owner.
  registration.serverIdIndependent =
    server.removeListener(sent) && !server.removeListener(sent);
  // Put it back: the dispatch order below expects primary,L1 again.
  server.addSentListener(
    [](const EspBleGattSendResult &) { appendTag(sentOrder, "L1"); });
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();

  auto &server = bluetooth.gattServer();
  EspBleGattCharacteristicConfig characteristicConfig;
  characteristicConfig.readable = true;
  characteristicConfig.writable = true;
  characteristicConfig.notifiable = true;
  const EspBleGattService service = server.addService(SERVICE_UUID);
  characteristic =
    server.addCharacteristic(service, CHARACTERISTIC_UUID, characteristicConfig);
  if (!service || !characteristic ||
      !server.setValue(characteristic, String("listener-ready")))
  {
    Serial.printf("CONFIG_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }

  registerListeners();

  EspBleConfig config;
  config.deviceName = "Bluedroid Multi Listener";
  if (!bluetooth.begin(config))
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
  Serial.println("MULTI_LISTENER_READY");
}

void loop()
{
  bluetooth.update();
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == 'r')
    {
      Serial.printf(
        "REGISTRATION accepted=%u fifth_refused=%u empty_refused=%u "
        "removed=%u unknown_failed=%u wrong_family_failed=%u "
        "right_family=%u server_ids=%u\n",
        static_cast<unsigned>(registration.accepted),
        registration.fifthRefused ? 1 : 0, registration.emptyRefused ? 1 : 0,
        registration.removedSecond ? 1 : 0,
        registration.removeUnknownFailed ? 1 : 0,
        registration.wrongFamilyFailed ? 1 : 0,
        registration.rightFamilySucceeded ? 1 : 0,
        registration.serverIdIndependent ? 1 : 0);
      Serial.printf("IDS first=%u second=%u distinct=%u\n",
        static_cast<unsigned>(registration.first),
        static_cast<unsigned>(registration.second),
        registration.first != registration.second ? 1 : 0);
    }
    else if (command == 'o')
    {
      Serial.printf("ORDER connected=%s written=%s subscription=%s sent=%s\n",
        connectedOrder.length() > 0 ? connectedOrder.c_str() : "none",
        writtenOrder.length() > 0 ? writtenOrder.c_str() : "none",
        subscriptionOrder.length() > 0 ? subscriptionOrder.c_str() : "none",
        sentOrder.length() > 0 ? sentOrder.c_str() : "none");
    }
    else if (command == 'n')
    {
      Serial.printf("NOTIFY_ACCEPTED %u\n",
        bluetooth.gattServer().notify(characteristic, String("listener-notify"))
          ? 1 : 0);
    }
  }
  delay(1);
}
