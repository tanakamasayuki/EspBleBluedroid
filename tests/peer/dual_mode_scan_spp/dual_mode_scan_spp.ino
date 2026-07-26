#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *SERVICE_UUID =
  "48e8c100-a176-4c75-8d8d-6f626c756564";
static constexpr const char *CHARACTERISTIC_UUID =
  "48e8c101-a176-4c75-8d8d-6f626c756564";
EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
EspBluedroidSppSessionId sppSessionId = 0;
EspBleConnectionId bleConnectionId = 0;
uint16_t characteristicHandle = 0;
size_t sppReceiveCount = 0;
size_t notificationCount = 0;
size_t trafficSppCallbackCount = 0;
size_t pendingTrafficWrites = 0;
size_t trafficBleDroppedBaseline = 0;
size_t trafficSppDroppedBaseline = 0;
size_t trafficRound = 0;
size_t priorityNotificationCount = 0;
bool trafficActive = false;
bool priorityTestActive = false;
bool initialized = false;
bool scanResultHandled = false;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void pumpTrafficWrites()
{
  static const uint8_t message[] = {0xd2, 0x00, 'G'};
  while (
    pendingTrafficWrites > 0 &&
    bluetooth.classic().spp().pendingWriteCount(sppSessionId) <
      EspBluedroidSpp::WriteQueueCapacity)
  {
    if (!bluetooth.classic().spp().write(
          sppSessionId, message, sizeof(message)))
    {
      return;
    }
    --pendingTrafficWrites;
  }
}

void reportTrafficRound(bool complete)
{
  size_t byteCount = 0;
  size_t validPacketCount = 0;
  uint8_t packet[3] = {};
  while (
    bluetooth.classic().spp().available(sppSessionId) >=
    sizeof(packet))
  {
    const size_t read = bluetooth.classic().spp().read(
      sppSessionId, packet, sizeof(packet));
    byteCount += read;
    if (
      read == sizeof(packet) &&
      packet[0] == 0xd1 && packet[1] == 0x00 &&
      packet[2] == 'P')
    {
      ++validPacketCount;
    }
  }
  const size_t bleEventDropped =
    bluetooth.droppedEventCount();
  const size_t sppEventDropped =
    bluetooth.classic().spp().droppedEventCount();
  ++trafficRound;
  Serial.printf(
    "DUAL_TRAFFIC_%s round=%u notifications=%u ring_packets=%u "
    "ring_bytes=%u spp_callbacks=%u ble_event_dropped=%u "
    "spp_event_dropped=%u spp_rx_dropped=%u "
    "spp_write_dropped=%u app_pending=%u\n",
    complete ? "COMPLETE" : "ROUND",
    static_cast<unsigned>(trafficRound),
    static_cast<unsigned>(notificationCount),
    static_cast<unsigned>(validPacketCount),
    static_cast<unsigned>(byteCount),
    static_cast<unsigned>(trafficSppCallbackCount),
    static_cast<unsigned>(
      bleEventDropped - trafficBleDroppedBaseline),
    static_cast<unsigned>(
      sppEventDropped - trafficSppDroppedBaseline),
    static_cast<unsigned>(
      bluetooth.classic().spp().droppedReceiveByteCount()),
    static_cast<unsigned>(
      bluetooth.classic().spp().droppedWriteCount()),
    static_cast<unsigned>(pendingTrafficWrites));
  notificationCount = 0;
  trafficSppCallbackCount = 0;
  trafficBleDroppedBaseline = bleEventDropped;
  trafficSppDroppedBaseline = sppEventDropped;
  if (complete)
  {
    Serial.printf("DUAL_GATT_UNSUBSCRIBE_ACCEPTED %u\n",
      bluetooth.unsubscribe(
        bleConnectionId, characteristicHandle, 5000) ? 1 : 0);
  }
}

void initializeBluetooth()
{
  if (!bluetooth.begin())
  {
    Serial.printf("DUAL_INIT_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (scanResultHandled || result.name != "Bluedroid Dual Peer") return;
    scanResultHandled = true;
    const bool stopped = bluetooth.scanner().stop();
    Serial.printf(
      "DUAL_BLE_SCAN_FOUND name=%s spp_sessions=%u stopped=%u context=%s\n",
      result.name.c_str(),
      static_cast<unsigned>(bluetooth.classic().spp().sessionCount()),
      stopped ? 1 : 0, contextName());
    Serial.printf("DUAL_BLE_CONNECT_ACCEPTED %u\n",
      bluetooth.connect(result) ? 1 : 0);
  });
  bluetooth.classic().spp().onConnected(
    [](const EspBluedroidSppSession &session) {
      sppSessionId = session.id;
      EspBleScanConfig scanConfig;
      scanConfig.active = true;
      scanConfig.durationSeconds = 10;
      Serial.printf(
        "DUAL_SPP_CONNECTED id=%u scan_started=%u context=%s\n",
        static_cast<unsigned>(session.id),
        bluetooth.scanner().start(scanConfig) ? 1 : 0,
        contextName());
    });
  bluetooth.classic().spp().onData([](const EspBluedroidSppData &event) {
    ++sppReceiveCount;
    if (trafficActive) ++trafficSppCallbackCount;
    Serial.printf("DUAL_SPP_RX id=%u length=%u hex=",
      static_cast<unsigned>(event.sessionId),
      static_cast<unsigned>(event.value.length()));
    for (size_t index = 0; index < event.value.length(); ++index)
    {
      Serial.printf("%02x", static_cast<uint8_t>(event.value[index]));
    }
    Serial.printf(" phase=%u ble_connections=%u context=%s\n",
      static_cast<unsigned>(sppReceiveCount),
      static_cast<unsigned>(bluetooth.connectionCount()), contextName());
    if (sppReceiveCount == 1)
    {
      while (bluetooth.classic().spp().available(event.sessionId) > 0)
      {
        bluetooth.classic().spp().read(event.sessionId);
      }
      Serial.printf("DUAL_GATT_DISCOVERY_ACCEPTED %u\n",
        bluetooth.discoverServices(bleConnectionId, 5000) ? 1 : 0);
    }
  });
  bluetooth.classic().spp().onDisconnected(
    [](const EspBluedroidSppSession &session) {
      Serial.printf(
        "DUAL_SPP_DISCONNECTED id=%u sessions=%u context=%s\n",
        static_cast<unsigned>(session.id),
        static_cast<unsigned>(bluetooth.classic().spp().sessionCount()),
        contextName());
    });
  bluetooth.classic().spp().onConnectionFailed(
    [](const EspBluedroidSppConnectionFailure &failure) {
      Serial.printf("DUAL_CONNECT_FAILED %s %s\n",
        failure.peerAddress.c_str(), failure.detail.c_str());
    });
  bluetooth.onConnected([](const EspBleConnection &connection) {
    bleConnectionId = connection.id;
    Serial.printf(
      "DUAL_BLE_CONNECTED id=%u spp_sessions=%u context=%s\n",
      static_cast<unsigned>(connection.id),
      static_cast<unsigned>(bluetooth.classic().spp().sessionCount()),
      contextName());
    const uint8_t message[] = {0xd0, 0x00, 'H'};
    Serial.printf("DUAL_SPP_WRITE_ACCEPTED %u\n",
      bluetooth.classic().spp().write(
        sppSessionId, message, sizeof(message)) ? 1 : 0);
  });
  bluetooth.onServicesDiscovered([](const EspBleGattResult &result) {
    bool found = false;
    for (size_t index = 0;
         index < bluetooth.discoveredCharacteristicCount(
           result.connectionId, SERVICE_UUID);
         ++index)
    {
      EspBleGattCharacteristicInfo characteristic;
      if (
        bluetooth.discoveredCharacteristic(
          result.connectionId, index, characteristic, SERVICE_UUID) &&
        characteristic.characteristicUuid.equalsIgnoreCase(
          CHARACTERISTIC_UUID))
      {
        characteristicHandle = characteristic.handle;
        found = characteristic.readable && characteristic.writable &&
          characteristic.notifiable;
        break;
      }
    }
    Serial.printf(
      "DUAL_GATT_DISCOVERED success=%u found=%u handle=%u "
      "spp_sessions=%u context=%s\n",
      result.success ? 1 : 0, found ? 1 : 0, characteristicHandle,
      static_cast<unsigned>(bluetooth.classic().spp().sessionCount()),
      contextName());
    Serial.printf("DUAL_GATT_READ_ACCEPTED %u\n",
      bluetooth.readCharacteristic(
        result.connectionId, characteristicHandle, 5000) ? 1 : 0);
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    const bool valid =
      result.success && result.value.length() == 3 &&
      static_cast<uint8_t>(result.value[0]) == 0xb0 &&
      static_cast<uint8_t>(result.value[1]) == 0x00 &&
      static_cast<uint8_t>(result.value[2]) == 0x52;
    Serial.printf(
      "DUAL_GATT_READ valid=%u spp_sessions=%u context=%s\n",
      valid ? 1 : 0,
      static_cast<unsigned>(bluetooth.classic().spp().sessionCount()),
      contextName());
    const uint8_t value[] = {0xb1, 0x00, 0x57};
    Serial.printf("DUAL_GATT_WRITE_ACCEPTED %u\n",
      bluetooth.writeCharacteristic(
        result.connectionId, characteristicHandle,
        value, sizeof(value), true, 5000) ? 1 : 0);
  });
  bluetooth.onCharacteristicWritten([](const EspBleGattResult &result) {
    Serial.printf(
      "DUAL_GATT_WRITTEN success=%u spp_sessions=%u context=%s\n",
      result.success ? 1 : 0,
      static_cast<unsigned>(bluetooth.classic().spp().sessionCount()),
      contextName());
    Serial.printf("DUAL_GATT_SUBSCRIBE_ACCEPTED %u\n",
      bluetooth.subscribe(
        result.connectionId, characteristicHandle, true, 5000) ? 1 : 0);
  });
  bluetooth.onSubscribed([](const EspBleGattResult &result) {
    trafficBleDroppedBaseline = bluetooth.droppedEventCount();
    trafficSppDroppedBaseline =
      bluetooth.classic().spp().droppedEventCount();
    trafficActive = true;
    Serial.printf(
      "DUAL_GATT_SUBSCRIBED success=%u spp_sessions=%u context=%s\n",
      result.success ? 1 : 0,
      static_cast<unsigned>(bluetooth.classic().spp().sessionCount()),
      contextName());
  });
  bluetooth.onNotification([](const EspBleGattNotification &notification) {
    if (priorityTestActive)
    {
      ++priorityNotificationCount;
      return;
    }
    ++notificationCount;
    const bool valid =
      notification.value.length() == 3 &&
      static_cast<uint8_t>(notification.value[0]) == 0xb2 &&
      static_cast<uint8_t>(notification.value[1]) == 0x00 &&
      static_cast<uint8_t>(notification.value[2]) == 0x4e;
    Serial.printf(
      "DUAL_GATT_NOTIFICATION valid=%u spp_sessions=%u context=%s\n",
      valid ? 1 : 0,
      static_cast<unsigned>(bluetooth.classic().spp().sessionCount()),
      contextName());
    ++pendingTrafficWrites;
    pumpTrafficWrites();
    Serial.printf("DUAL_SPP_DURING_GATT_WRITE_PENDING %u\n",
      static_cast<unsigned>(pendingTrafficWrites));
  });
  bluetooth.onUnsubscribed([](const EspBleGattResult &result) {
    if (priorityTestActive)
    {
      priorityTestActive = false;
      Serial.printf(
        "DUAL_PRIORITY_COMPLETE notifications=%u dropped=%u "
        "success=%u context=%s\n",
        static_cast<unsigned>(priorityNotificationCount),
        static_cast<unsigned>(bluetooth.droppedEventCount()),
        result.success ? 1 : 0, contextName());
      return;
    }
    Serial.printf(
      "DUAL_GATT_UNSUBSCRIBED success=%u spp_sessions=%u context=%s\n",
      result.success ? 1 : 0,
      static_cast<unsigned>(bluetooth.classic().spp().sessionCount()),
      contextName());
    Serial.printf("DUAL_BLE_DISCONNECT_ACCEPTED %u\n",
      bluetooth.disconnect(result.connectionId) ? 1 : 0);
    Serial.printf("DUAL_SPP_DISCONNECT_ACCEPTED %u\n",
      bluetooth.classic().spp().disconnect(sppSessionId) ? 1 : 0);
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf(
      "DUAL_BLE_DISCONNECTED id=%u ble_connections=%u context=%s\n",
      static_cast<unsigned>(connection.id),
      static_cast<unsigned>(bluetooth.connectionCount()), contextName());
  });
  Serial.println("DUAL_HOST_READY");
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();
}

void loop()
{
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == 'i' && !initialized)
    {
      initialized = true;
      initializeBluetooth();
    }
    else if (command == 'c' && bluetooth.initialized())
    {
      const String address = Serial.readStringUntil('\n');
      Serial.printf("DUAL_CONNECT_ACCEPTED %u\n",
        bluetooth.classic().spp().connect(address.c_str()) ? 1 : 0);
    }
#ifdef ESP_BLE_BLUEDROID_TESTING
    else if (command == 'p' && bluetooth.initialized())
    {
      priorityTestActive = true;
      priorityNotificationCount = 0;
      size_t acceptedNotifications = 0;
      for (size_t index = 0; index < 8; ++index)
      {
        EspBleGattNotification notification;
        notification.connectionId = 77;
        notification.value = String(static_cast<char>('0' + index));
        if (bluetooth.injectNotificationForTest(notification))
        {
          ++acceptedNotifications;
        }
      }
      EspBleGattResult result;
      result.operation = EspBleGattOperation::Unsubscribe;
      result.connectionId = 77;
      result.success = true;
      const bool controlAccepted =
        bluetooth.injectGattResultForTest(result);
      Serial.printf(
        "DUAL_PRIORITY_INJECTED notifications=%u control=%u\n",
        static_cast<unsigned>(acceptedNotifications),
        controlAccepted ? 1 : 0);
    }
#endif
    else if (
      (command == 'q' || command == 'r') &&
      sppSessionId != 0)
    {
      reportTrafficRound(command == 'q');
    }
  }
  bluetooth.update();
  pumpTrafficWrites();
  delay(1);
}
