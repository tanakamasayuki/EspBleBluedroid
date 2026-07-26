#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;
String comparisonPeer;

void setup()
{
  Serial.begin(115200);

  bluetooth.classic().onNumericComparisonRequested(
    [](const EspBluedroidClassicNumericComparison &event) {
      comparisonPeer = event.peerAddress;
      Serial.printf("Compare %06u with %s, then enter y or n\n",
        static_cast<unsigned>(event.value), event.peerAddress.c_str());
    });
  bluetooth.classic().onSecurityChanged(
    [](const EspBluedroidClassicSecurityChanged &event) {
      Serial.printf("Classic authentication %s: peer=%s status=%d\n",
        event.success ? "succeeded" : "failed",
        event.peerAddress.c_str(), event.status);
    });
  bluetooth.classic().spp().onConnected(
    [](const EspBluedroidSppSession &session) {
      Serial.printf("secure SPP connected: authenticated=%u encrypted=%u\n",
        session.authenticated ? 1 : 0, session.encrypted ? 1 : 0);
    });
  bluetooth.classic().spp().onData(
    [](const EspBluedroidSppData &event) {
      bluetooth.classic().spp().write(event.sessionId, event.value);
    });

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Secure SPP";
  config.classicSecurity.enabled = true;
  config.classicSecurity.ioCapability =
    EspBluedroidClassicSecurityIoCapability::DisplayYesNo;
  if (!bluetooth.begin(config))
  {
    Serial.printf("begin failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  EspBluedroidSppServerConfig server;
  server.serviceName = "EspBleBluedroid Secure";
  server.security =
    EspBluedroidSppSecurity::AuthenticatedEncrypted;
  if (!bluetooth.classic().spp().startServer(server))
  {
    Serial.printf("startServer failed: %s\n", bluetooth.lastErrorName());
  }
}

void loop()
{
  if (Serial.available() && !comparisonPeer.isEmpty())
  {
    const char response = Serial.read();
    if (response == 'y' || response == 'n')
    {
      bluetooth.classic().confirmNumericComparison(
        comparisonPeer.c_str(), response == 'y');
      comparisonPeer = "";
    }
  }
  bluetooth.update();
  delay(1);
}
