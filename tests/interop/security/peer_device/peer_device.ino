// Cross-stack security: this library as the pairing central against an EspBle
// (NimBLE) peripheral. This is the `peer_device` half — the board running the
// library under test — while the ESP32-S3 running EspBle is the parent fixture.
//
// `peer/security_bond` and `peer/security_passkey` pin bonding, the static-passkey
// flow and the attribute tiers with Bluedroid on both ends, where a shared
// assumption about the association model cancels itself out. SMP is negotiated
// between two host stacks, so the numbers that matter — encrypted, authenticated,
// bonded, the key size — are the ones both sides report about the same link.
//
// begin() is deliberately not called from setup(): the security configuration is
// chosen by the mode command, so one firmware serves both the Just Works test and
// the static-passkey test.
//
// Every step is driven by a serial command, so a failure names the step.

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Interop UUIDs live in the 01xx suite-tag range (tests/TEST_PLAN.md).
static constexpr const char *SERVICE_UUID =
  "01040000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *ENCRYPTED_UUID =
  "01040001-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *AUTHENTICATED_UUID =
  "01040002-b1dd-4d00-9e5a-627564726f69";

// The same passkey as `peer/security_passkey`, per the shared-expectations rule
// in tests/TEST_PLAN.md.
static constexpr uint32_t PASSKEY = 438209;

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
bool started = false;
bool connectionRequested = false;
EspBleConnectionId connectionId = 0;

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

void installCallbacks()
{
  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    Serial.printf("CONNECTED id=%u encrypted=%u bonded=%u context=%s\n",
      static_cast<unsigned>(connection.id), connection.encrypted ? 1 : 0,
      connection.bonded ? 1 : 0, contextName());
  });
  bluetooth.onSecurityChanged([](const EspBleSecurityChanged &event) {
    Serial.printf(
      "SECURITY success=%u encrypted=%u authenticated=%u bonded=%u key=%u context=%s\n",
      event.success ? 1 : 0, event.connection.encrypted ? 1 : 0,
      event.connection.authenticated ? 1 : 0, event.connection.bonded ? 1 : 0,
      event.connection.encryptionKeySize, contextName());
  });
  bluetooth.onPasskeyDisplayed([](const EspBlePasskeyDisplayed &event) {
    Serial.printf("PASSKEY_DISPLAYED passkey=%06u context=%s\n",
      static_cast<unsigned>(event.passkey), contextName());
  });
  bluetooth.onServicesDiscovered([](const EspBleGattResult &result) {
    Serial.printf("DISCOVERY success=%u services=%u context=%s\n",
      result.success ? 1 : 0,
      static_cast<unsigned>(
        bluetooth.discoveredServiceCount(result.connectionId)),
      contextName());
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    Serial.printf("READ success=%u error=%s value=%s context=%s\n",
      result.success ? 1 : 0, errorName(result.error), result.value.c_str(),
      contextName());
  });
  bluetooth.onCharacteristicWritten([](const EspBleGattResult &result) {
    Serial.printf("WRITE success=%u error=%s context=%s\n",
      result.success ? 1 : 0, errorName(result.error), contextName());
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf("DISCONNECTED id=%u encrypted=%u bonded=%u context=%s\n",
      static_cast<unsigned>(connection.id), connection.encrypted ? 1 : 0,
      connection.bonded ? 1 : 0, contextName());
    connectionRequested = false;
    connectionId = 0;
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
}

bool startWithMode(char mode)
{
  // The two tests of this suite share one flash, so switching mode has to go
  // through end(): a second begin() with a different security configuration is
  // rejected (InvalidState), which is the contract and not a workaround.
  if (started)
  {
    bluetooth.end();
    started = false;
  }

  EspBleConfig config;
  config.deviceName = "Bluedroid Security Central";
  config.security.enabled = true;
  config.security.bonding = true;
  config.security.pairOnConnect = true;
  if (mode == 'p')
  {
    config.security.mitm = true;
    config.security.ioCapability = EspBleSecurityIoCapability::DisplayOnly;
    config.security.staticPasskeyEnabled = true;
    config.security.staticPasskey = PASSKEY;
  }
  return bluetooth.begin(config);
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();
  installCallbacks();
  Serial.println("INTEROP_SECURITY_READY");
}

void loop()
{
  bluetooth.update();
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == '?')
    {
      // Answer on request instead of relying on the startup line: this board
      // finishes booting while the monitor is still attaching, so a test that
      // waited for the boot output alone would depend on that timing.
      Serial.printf("READY_STATE started=%u\n", started ? 1 : 0);
    }
    else if (command == 'j' || command == 'p')
    {
      started = startWithMode(command);
      Serial.printf("MODE_STARTED mode=%c ok=%u error=%s\n", command,
        started ? 1 : 0, bluetooth.lastErrorName());
    }
    else if (command == 'x')
    {
      // Bonds live in NVS, so a bond left by an earlier run would let this one
      // skip pairing altogether and pass without exercising it.
      const bool cleared = bluetooth.deleteAllBonds();
      Serial.printf("BONDS_CLEARED success=%u count=%u\n", cleared ? 1 : 0,
        static_cast<unsigned>(bluetooth.bondCount()));
    }
    else if (command == 'b')
    {
      Serial.printf("BONDS count=%u\n",
        static_cast<unsigned>(bluetooth.bondCount()));
    }
    else if (command == 'c')
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
    else if (command == 'e')
    {
      Serial.printf("ENCRYPTED_READ_REQUESTED %u\n",
        bluetooth.readCharacteristic(
          connectionId, SERVICE_UUID, ENCRYPTED_UUID, 5000) ? 1 : 0);
    }
    else if (command == 'E')
    {
      Serial.printf("ENCRYPTED_WRITE_REQUESTED %u\n",
        bluetooth.writeCharacteristic(connectionId, SERVICE_UUID,
          ENCRYPTED_UUID, String("central-encrypted-write"), true, 5000)
          ? 1 : 0);
    }
    else if (command == 'a')
    {
      // On an unauthenticated link this is expected to be refused by the peer.
      // Which tier a link may reach is the contract, so it is exercised rather
      // than avoided.
      Serial.printf("AUTHENTICATED_READ_REQUESTED %u\n",
        bluetooth.readCharacteristic(
          connectionId, SERVICE_UUID, AUTHENTICATED_UUID, 5000) ? 1 : 0);
    }
    else if (command == 'A')
    {
      Serial.printf("AUTHENTICATED_WRITE_REQUESTED %u\n",
        bluetooth.writeCharacteristic(connectionId, SERVICE_UUID,
          AUTHENTICATED_UUID, String("central-authenticated-write"), true, 5000)
          ? 1 : 0);
    }
    else if (command == 'X')
    {
      Serial.printf("DISCONNECT_REQUESTED %u\n",
        bluetooth.disconnect(connectionId) ? 1 : 0);
    }
  }
  delay(1);
}
