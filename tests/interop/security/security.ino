// The EspBle (NimBLE) half of the cross-stack security scenario, running on an
// ESP32-S3 against the released EspBle pinned in sketch.yaml.
//
// Pairing is the part of BLE where two host stacks have the most room to disagree
// and the least room to recover: the SMP exchange, which association model each
// side picks from the IO capabilities, what "authenticated" ends up meaning, and
// whether an attribute permission tier is enforced. `peer/security_bond` and
// `peer/security_passkey` pin all of that with Bluedroid on both ends, where a
// shared assumption about the association model cancels itself out.
//
// begin() is deliberately not called from setup(): the security configuration is
// chosen by the mode command, so one firmware serves both the Just Works test and
// the static-passkey test instead of the suite needing two peripherals.
//
// Two characteristics carry the two permission tiers, so what each mode is
// allowed to reach is observable rather than assumed:
//   * encrypted read/write  — reachable once the link is encrypted;
//   * authenticated read/write — needs an authenticated (MITM-protected) link.
//
// Output is prefixed ESPBLE_ so a log line never leaves it ambiguous which stack
// produced it.

#include <EspBle.h>

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

EspBle ble;
bool started = false;
EspBleGattService service;
EspBleGattCharacteristic encrypted;
EspBleGattCharacteristic authenticated;

bool startWithMode(char mode)
{
  // The two tests of this suite share one flash, so switching mode has to go
  // through end(): a second begin() with a different security configuration is
  // rejected (InvalidState), which is the contract and not a workaround.
  if (started)
  {
    ble.end();
    started = false;
  }

  EspBleConfig config;
  config.deviceName = "EspBle Security Peer";
  config.security.enabled = true;
  config.security.bonding = true;
  config.security.pairOnConnect = true;
  if (mode == 'p')
  {
    // Static passkey: MITM protection with a value the peer already knows, which
    // is the only passkey model a test can drive unattended. KeyboardOnly here
    // against the central's DisplayOnly is what selects Passkey Entry — the
    // association model that yields an *authenticated* link. Two DisplayOnly
    // sides would fall back to Just Works and the link would be unauthenticated
    // however the passkey is configured.
    config.security.mitm = true;
    config.security.ioCapability = EspBleSecurityIoCapability::KeyboardOnly;
    config.security.staticPasskeyEnabled = true;
    config.security.staticPasskey = PASSKEY;
  }
  if (!ble.begin(config))
  {
    return false;
  }
  // Back to the published values: the two tests share one flash, so a value the
  // previous test wrote would otherwise be what the next one reads.
  ble.gattServer().setValue(encrypted, String("encrypted-ready"));
  ble.gattServer().setValue(authenticated, String("authenticated-ready"));
  ble.advertising().setName(config.deviceName);
  ble.advertising().addServiceUuid(SERVICE_UUID);
  return ble.advertising().start();
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  auto &server = ble.gattServer();
  EspBleGattCharacteristicConfig encryptedConfig;
  encryptedConfig.readable = true;
  encryptedConfig.writable = true;
  encryptedConfig.encryptedRead = true;
  encryptedConfig.encryptedWrite = true;
  EspBleGattCharacteristicConfig authenticatedConfig;
  authenticatedConfig.readable = true;
  authenticatedConfig.writable = true;
  authenticatedConfig.authenticatedRead = true;
  authenticatedConfig.authenticatedWrite = true;

  service = server.addService(SERVICE_UUID);
  encrypted = server.addCharacteristic(service, ENCRYPTED_UUID, encryptedConfig);
  authenticated =
    server.addCharacteristic(service, AUTHENTICATED_UUID, authenticatedConfig);
  if (!service.valid() || !encrypted.valid() || !authenticated.valid() ||
      !server.setValue(encrypted, String("encrypted-ready")) ||
      !server.setValue(authenticated, String("authenticated-ready")))
  {
    Serial.printf("ESPBLE_CONFIG_FAILED %s %s\n", ble.lastErrorName(),
      ble.lastErrorDetail().c_str());
    return;
  }

  ble.onConnected([](const EspBleConnection &connection) {
    Serial.printf("ESPBLE_CONNECTED id=%u encrypted=%u bonded=%u\n",
      static_cast<unsigned>(connection.id), connection.encrypted ? 1 : 0,
      connection.bonded ? 1 : 0);
  });
  ble.onSecurityChanged([](const EspBleSecurityChanged &event) {
    Serial.printf(
      "ESPBLE_SECURITY success=%u encrypted=%u authenticated=%u bonded=%u key=%u\n",
      event.success ? 1 : 0, event.connection.encrypted ? 1 : 0,
      event.connection.authenticated ? 1 : 0, event.connection.bonded ? 1 : 0,
      event.connection.encryptionKeySize);
  });
  ble.onPasskeyDisplayed([](const EspBlePasskeyDisplayed &event) {
    Serial.printf("ESPBLE_PASSKEY_DISPLAYED passkey=%06u\n",
      static_cast<unsigned>(event.passkey));
  });
  ble.onDisconnected([](const EspBleConnection &connection) {
    Serial.printf("ESPBLE_DISCONNECTED id=%u encrypted=%u bonded=%u\n",
      static_cast<unsigned>(connection.id), connection.encrypted ? 1 : 0,
      connection.bonded ? 1 : 0);
  });
  server.onWritten([](const EspBleGattWrite &write) {
    // Which tier accepted the write, so a write that landed on the wrong
    // characteristic cannot pass as the right one.
    Serial.printf("ESPBLE_WRITE tier=%s value=%s\n",
      write.characteristic == authenticated ? "authenticated" : "encrypted",
      write.value.c_str());
  });

  Serial.println("ESPBLE_SECURITY_PEER_READY");
}

void loop()
{
  ble.update();
  if (Serial.available())
  {
    const int command = Serial.read();
    if (command == '?')
    {
      // Answer on request. The board finishes booting while the other one is
      // still being flashed, so a test that waited for the startup line alone
      // would depend on when the monitor started reading.
      Serial.printf("ESPBLE_SECURITY_STATE started=%u\n", started ? 1 : 0);
    }
    else if (command == 'j' || command == 'p')
    {
      started = startWithMode(static_cast<char>(command));
      Serial.printf("ESPBLE_MODE_STARTED mode=%c ok=%u error=%s\n",
        static_cast<char>(command), started ? 1 : 0, ble.lastErrorName());
    }
    else if (command == 'x')
    {
      // Bonds live in NVS, so a bond left by an earlier run would let this one
      // skip pairing altogether and pass without exercising it.
      const bool cleared = ble.deleteAllBonds();
      Serial.printf("ESPBLE_BONDS_CLEARED success=%u count=%u\n",
        cleared ? 1 : 0, static_cast<unsigned>(ble.bondCount()));
    }
    else if (command == 'b')
    {
      Serial.printf("ESPBLE_BONDS count=%u\n",
        static_cast<unsigned>(ble.bondCount()));
    }
  }
  delay(1);
}
