// The peripheral half of the connection lifecycle, which used to exist on the air
// but not in the public API.
//
// A peer that connects to this device's GATT Server produced no onConnected(), no
// MTU event, no entry in connection(), and its pairing was dropped: the security
// callback bailed out unless a *central* link was active. Everything an
// application can observe about a link it opened itself — the runtime ID, the peer
// address, the negotiated MTU, the connection parameters, the encryption state,
// the HCI disconnection reason — is reported here as well, with
// `localRole = Peripheral`.
//
// The peer side is a raw Arduino-ESP32 BLE client, so nothing in this scenario is
// two halves of this library agreeing with each other.

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *SERVICE_UUID =
  "00070000-b1dd-4d00-9e5a-627564726f69";
// Plain read/write: reachable before any pairing, so the reported connection ID
// can be checked against the snapshot on an unencrypted link.
static constexpr const char *PLAIN_UUID =
  "00070001-b1dd-4d00-9e5a-627564726f69";
// Encrypted read: the peer has to pair before it can read this, which is what
// puts a security event on a peripheral link.
static constexpr const char *ENCRYPTED_UUID =
  "00070002-b1dd-4d00-9e5a-627564726f69";

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
EspBleGattCharacteristic plain;
EspBleGattCharacteristic encrypted;
// Captured from the events, reported on request: a contract assertion must not
// depend on serial output that may be lost while the port reopens.
EspBleConnectionId connectedId = 0;
EspBleConnectionId writeId = 0;
uint16_t reportedMtu = 0;
bool securityReported = false;
bool securityEncrypted = false;
bool securityBonded = false;
EspBleRole securityRole = EspBleRole::Central;
EspBleConnectionId securityId = 0;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

const char *roleName(EspBleRole role)
{
  return role == EspBleRole::Peripheral ? "peripheral" : "central";
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();

  auto &server = bluetooth.gattServer();
  EspBleGattCharacteristicConfig plainConfig;
  plainConfig.readable = true;
  plainConfig.writable = true;
  EspBleGattCharacteristicConfig encryptedConfig;
  encryptedConfig.readable = true;
  encryptedConfig.encryptedRead = true;

  const EspBleGattService service = server.addService(SERVICE_UUID);
  plain = server.addCharacteristic(service, PLAIN_UUID, plainConfig);
  encrypted = server.addCharacteristic(service, ENCRYPTED_UUID, encryptedConfig);
  if (!service || !plain || !encrypted ||
      !server.setValue(plain, String("plain-ready")) ||
      !server.setValue(encrypted, String("encrypted-ready")))
  {
    Serial.printf("CONFIG_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }
  server.onWritten([](const EspBleGattWrite &write) {
    // The ID a Server event carries has to be the ID the snapshot knows, or an
    // application cannot look up the link a write came from.
    writeId = write.connectionId;
    Serial.printf("WRITE id=%u value=%s context=%s\n",
      static_cast<unsigned>(write.connectionId), write.value.c_str(),
      contextName());
  });

  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectedId = connection.id;
    Serial.printf(
      "CONNECTED id=%u role=%s mtu=%u peer=%s encrypted=%u context=%s\n",
      static_cast<unsigned>(connection.id), roleName(connection.localRole),
      static_cast<unsigned>(connection.mtu), connection.peerAddress.c_str(),
      connection.encrypted ? 1 : 0, contextName());
  });
  bluetooth.onMtuChanged([](const EspBleMtuChanged &event) {
    // The central drives the exchange, so this side only observes it — which it
    // could not do at all before.
    reportedMtu = event.connection.mtu;
    Serial.printf("MTU previous=%u mtu=%u role=%s context=%s\n",
      static_cast<unsigned>(event.previousMtu),
      static_cast<unsigned>(event.connection.mtu),
      roleName(event.connection.localRole), contextName());
  });
  bluetooth.onConnectionParametersUpdated(
    [](const EspBleConnection &connection) {
      Serial.printf(
        "PARAMETERS interval=%u latency=%u timeout=%u role=%s context=%s\n",
        static_cast<unsigned>(connection.connectionInterval),
        static_cast<unsigned>(connection.peripheralLatency),
        static_cast<unsigned>(connection.supervisionTimeout),
        roleName(connection.localRole), contextName());
    });
  bluetooth.onSecurityChanged([](const EspBleSecurityChanged &event) {
    securityReported = true;
    securityEncrypted = event.connection.encrypted;
    securityBonded = event.connection.bonded;
    securityRole = event.connection.localRole;
    securityId = event.connection.id;
    Serial.printf(
      "SECURITY success=%u encrypted=%u authenticated=%u bonded=%u key=%u "
      "role=%s id=%u context=%s\n",
      event.success ? 1 : 0, event.connection.encrypted ? 1 : 0,
      event.connection.authenticated ? 1 : 0, event.connection.bonded ? 1 : 0,
      event.connection.encryptionKeySize, roleName(event.connection.localRole),
      static_cast<unsigned>(event.connection.id), contextName());
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf(
      "DISCONNECTED id=%u role=%s reason=%d count=%u context=%s\n",
      static_cast<unsigned>(connection.id), roleName(connection.localRole),
      connection.disconnectReason,
      static_cast<unsigned>(bluetooth.connectionCount()), contextName());
  });

  EspBleConfig config;
  config.deviceName = "Bluedroid Peripheral Link";
  config.security.enabled = true;
  config.security.bonding = true;
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
  Serial.println("PERIPHERAL_LINK_READY");
}

void loop()
{
  bluetooth.update();
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == 's')
    {
      // The snapshot, looked up by the ID the connect event reported. A peripheral
      // link used to be absent from both.
      EspBleConnection snapshot;
      const bool found = bluetooth.connection(connectedId, snapshot);
      Serial.printf(
        "SNAPSHOT count=%u found=%u id=%u role=%s mtu=%u peer=%s "
        "interval=%u encrypted=%u bonded=%u\n",
        static_cast<unsigned>(bluetooth.connectionCount()), found ? 1 : 0,
        static_cast<unsigned>(snapshot.id), roleName(snapshot.localRole),
        static_cast<unsigned>(snapshot.mtu), snapshot.peerAddress.c_str(),
        static_cast<unsigned>(snapshot.connectionInterval),
        snapshot.encrypted ? 1 : 0, snapshot.bonded ? 1 : 0);
    }
    else if (command == 'w')
    {
      Serial.printf("WRITE_ID id=%u matches=%u\n",
        static_cast<unsigned>(writeId),
        writeId != 0 && writeId == connectedId ? 1 : 0);
    }
    else if (command == 'e')
    {
      Serial.printf(
        "SECURITY_STATE reported=%u encrypted=%u bonded=%u role=%s matches=%u\n",
        securityReported ? 1 : 0, securityEncrypted ? 1 : 0,
        securityBonded ? 1 : 0, roleName(securityRole),
        securityId != 0 && securityId == connectedId ? 1 : 0);
    }
    else if (command == 'm')
    {
      Serial.printf("MTU_STATE mtu=%u\n", static_cast<unsigned>(reportedMtu));
    }
    else if (command == 'x')
    {
      Serial.printf("BONDS_CLEARED success=%u count=%u\n",
        bluetooth.deleteAllBonds() ? 1 : 0,
        static_cast<unsigned>(bluetooth.bondCount()));
    }
  }
  delay(1);
}
