#include <EspBleBluedroid.h>

static constexpr size_t MaxFound = 10;

EspBleBluedroid bluetooth;
EspBleScanResult found[MaxFound];
size_t foundCount = 0;
EspBleConnectionId currentId = 0;

static bool alreadyListed(const EspBleScanResult &result)
{
  for (size_t index = 0; index < foundCount; ++index)
  {
    if (found[index].address == result.address) return true;
  }
  return false;
}

static void printConnection(const EspBleConnection &connection)
{
  Serial.printf(
    "CONNECTION id=%lu handle=%u peer=%s(type=%u) role=%s\n"
    "  mtu=%u maxNotificationPayload=%u\n"
    "  interval=%u latency=%u timeout=%u\n"
    "  encrypted=%u authenticated=%u bonded=%u keySize=%u\n",
    static_cast<unsigned long>(connection.id),
    connection.handle,
    connection.peerAddress.c_str(),
    static_cast<unsigned>(connection.peerAddressType),
    connection.localRole == EspBleRole::Central ? "Central" : "Peripheral",
    connection.mtu,
    static_cast<unsigned>(connection.maximumNotificationPayload()),
    connection.connectionInterval,
    connection.peripheralLatency,
    connection.supervisionTimeout,
    connection.encrypted ? 1 : 0,
    connection.authenticated ? 1 : 0,
    connection.bonded ? 1 : 0,
    connection.encryptionKeySize);
}

static void printBonds()
{
  const size_t count = bluetooth.bondCount();
  Serial.printf("BONDS count=%u\n", static_cast<unsigned>(count));
  for (size_t index = 0; index < count; ++index)
  {
    EspBleBond bond;
    if (bluetooth.bond(index, bond))
    {
      Serial.printf("  [%u] %s type=%u\n",
        static_cast<unsigned>(index),
        bond.peerAddress.c_str(),
        static_cast<unsigned>(bond.peerAddressType));
    }
  }
}

static void printCounters()
{
  Serial.printf(
    "COUNTERS connections=%u droppedEvents=%u droppedScanResults=%u\n",
    static_cast<unsigned>(bluetooth.connectionCount()),
    static_cast<unsigned>(bluetooth.droppedEventCount()),
    static_cast<unsigned>(bluetooth.scanner().droppedResultCount()));
}

static void startScan()
{
  foundCount = 0;
  EspBleScanConfig config;
  config.active = true;
  Serial.printf("SCAN restart success=%u - send a list number to connect\n",
    bluetooth.scanner().start(config) ? 1 : 0);
}

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Inspector";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE init failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    currentId = connection.id;
    printConnection(connection);
  });
  bluetooth.onMtuChanged([](const EspBleMtuChanged &event) {
    printConnection(event.connection);
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    currentId = 0;
    Serial.printf("DISCONNECTED id=%lu reason=%d\n",
      static_cast<unsigned long>(connection.id),
      connection.disconnectReason);
  });
  bluetooth.onConnectionFailed([](const EspBleConnectionFailure &failure) {
    Serial.printf("CONNECT_FAILED peer=%s detail=%s\n",
      failure.peerAddress.c_str(), failure.detail.c_str());
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (!result.connectable || foundCount >= MaxFound ||
        alreadyListed(result))
    {
      return;
    }
    found[foundCount] = result;
    Serial.printf("[%u] %s rssi=%d%s%s\n",
      static_cast<unsigned>(foundCount),
      result.address.c_str(),
      result.rssi,
      result.hasName() ? " name=" : "",
      result.hasName() ? result.name.c_str() : "");
    ++foundCount;
  });

  Serial.println(
    "Commands: 0-9 connect, s rescan, d disconnect, b bonds, q counters");
  startScan();
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command >= '0' && command <= '9')
    {
      const size_t index = static_cast<size_t>(command - '0');
      if (index < foundCount)
      {
        bluetooth.scanner().stop();
        Serial.printf("CONNECT [%u] %s accepted=%u\n",
          static_cast<unsigned>(index),
          found[index].address.c_str(),
          bluetooth.connect(found[index]) ? 1 : 0);
      }
    }
    else if (command == 's')
    {
      startScan();
    }
    else if (command == 'd')
    {
      Serial.printf("DISCONNECT accepted=%u\n",
        bluetooth.disconnect(currentId) ? 1 : 0);
    }
    else if (command == 'b')
    {
      printBonds();
    }
    else if (command == 'q')
    {
      printCounters();
    }
  }

  bluetooth.update();
  delay(1);
}
