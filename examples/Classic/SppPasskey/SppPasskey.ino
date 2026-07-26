#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;
String passkeyPeerAddress;

void setup()
{
  Serial.begin(115200);
  delay(500);

  bluetooth.classic().onPasskeyRequested(
    [](const EspBluedroidClassicPasskeyRequested &event) {
      passkeyPeerAddress = event.peerAddress;
      Serial.printf(
        "Enter the six-digit passkey shown by %s:\n",
        event.peerAddress.c_str());
    });
  bluetooth.classic().onPasskeyDisplayed(
    [](const EspBluedroidClassicPasskeyDisplayed &event) {
      Serial.printf("Enter %06u on %s\n",
        static_cast<unsigned>(event.passkey),
        event.peerAddress.c_str());
    });
  bluetooth.classic().onSecurityChanged(
    [](const EspBluedroidClassicSecurityChanged &event) {
      Serial.printf("Security %s for %s (status=%d)\n",
        event.success ? "succeeded" : "failed",
        event.peerAddress.c_str(), event.status);
    });
  bluetooth.classic().spp().onConnected(
    [](const EspBluedroidSppSession &session) {
      Serial.printf(
        "Secure SPP connected: authenticated=%u encrypted=%u\n",
        session.authenticated ? 1 : 0, session.encrypted ? 1 : 0);
    });
  bluetooth.classic().spp().onData(
    [](const EspBluedroidSppData &event) {
      bluetooth.classic().spp().write(event.sessionId, event.value);
    });

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Passkey";
  config.classicSecurity.enabled = true;
  config.classicSecurity.ioCapability =
    EspBluedroidClassicSecurityIoCapability::KeyboardOnly;
  if (!bluetooth.begin(config))
  {
    Serial.printf("begin failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  EspBluedroidSppServerConfig server;
  server.serviceName = "EspBleBluedroid Passkey";
  server.security =
    EspBluedroidSppSecurity::AuthenticatedEncrypted;
  if (!bluetooth.classic().spp().startServer(server))
  {
    Serial.printf("startServer failed: %s\n",
      bluetooth.lastErrorName());
  }
}

void loop()
{
  bluetooth.update();
  if (!passkeyPeerAddress.isEmpty() && Serial.available())
  {
    const uint32_t passkey =
      static_cast<uint32_t>(Serial.parseInt());
    const bool accepted = bluetooth.classic().providePasskey(
      passkeyPeerAddress.c_str(), passkey);
    Serial.printf("Passkey reply accepted: %u\n", accepted ? 1 : 0);
    passkeyPeerAddress = "";
  }
  delay(1);
}
