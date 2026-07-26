#include <EspBleBluedroid.h>
#include <esp_bt_device.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
String passkeyAddress;
bool initialized = false;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

String localAddress()
{
  const uint8_t *address = esp_bt_dev_get_address();
  char value[18];
  snprintf(value, sizeof(value), "%02x:%02x:%02x:%02x:%02x:%02x",
    address[0], address[1], address[2], address[3], address[4], address[5]);
  return String(value);
}

bool startSecureServer(
  EspBluedroidClassicSecurityIoCapability capability)
{
  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Passkey";
  config.classicSecurity.enabled = true;
  config.classicSecurity.ioCapability = capability;
  if (!bluetooth.begin(config))
  {
    Serial.printf("SPP_PASSKEY_INIT_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return false;
  }
  bluetooth.classic().deleteAllBonds();
  EspBluedroidSppServerConfig server;
  server.serviceName = "EspBleBluedroid Passkey";
  server.security =
    EspBluedroidSppSecurity::AuthenticatedEncrypted;
  if (!bluetooth.classic().spp().startServer(server))
  {
    Serial.printf("SPP_PASSKEY_SERVER_FAILED %s\n",
      bluetooth.lastErrorName());
    return false;
  }
  return true;
}

void initializeBluetooth()
{
  bluetooth.classic().onPasskeyRequested(
    [](const EspBluedroidClassicPasskeyRequested &event) {
      passkeyAddress = event.peerAddress;
      Serial.printf("SPP_PASSKEY_REQUESTED address=%s context=%s\n",
        event.peerAddress.c_str(), contextName());
    });
  bluetooth.classic().onPasskeyDisplayed(
    [](const EspBluedroidClassicPasskeyDisplayed &event) {
      Serial.printf(
        "SPP_PASSKEY_DISPLAYED address=%s passkey=%06u context=%s\n",
        event.peerAddress.c_str(), static_cast<unsigned>(event.passkey),
        contextName());
    });
  bluetooth.classic().onSecurityChanged(
    [](const EspBluedroidClassicSecurityChanged &event) {
      Serial.printf(
        "SPP_PASSKEY_SECURITY address=%s success=%u status=%d "
        "context=%s\n",
        event.peerAddress.c_str(), event.success ? 1 : 0, event.status,
        contextName());
    });
  bluetooth.classic().spp().onServerStarted([]() {
    Serial.printf("SPP_PASSKEY_READY address=%s\n",
      localAddress().c_str());
  });
  bluetooth.classic().spp().onConnected(
    [](const EspBluedroidSppSession &session) {
      Serial.printf(
        "SPP_PASSKEY_CONNECTED id=%u authenticated=%u encrypted=%u "
        "incoming=%u context=%s\n",
        static_cast<unsigned>(session.id),
        session.authenticated ? 1 : 0, session.encrypted ? 1 : 0,
        session.incoming ? 1 : 0, contextName());
    });
  bluetooth.classic().spp().onData([](const EspBluedroidSppData &event) {
    bluetooth.classic().spp().write(event.sessionId, event.value);
  });
  bluetooth.classic().spp().onDisconnected(
    [](const EspBluedroidSppSession &session) {
      Serial.printf("SPP_PASSKEY_DISCONNECTED id=%u context=%s\n",
        static_cast<unsigned>(session.id), contextName());
    });

  if (!startSecureServer(
        EspBluedroidClassicSecurityIoCapability::KeyboardOnly)) return;
  const bool invalid = bluetooth.classic().providePasskey(
    "00:11:22:33:44:55", 1000000);
  Serial.printf("SPP_PASSKEY_INVALID_REJECTED %u error=%s\n",
    invalid ? 0 : 1, bluetooth.lastErrorName());

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
    else if (command == 'k' && !passkeyAddress.isEmpty())
    {
      const uint32_t passkey =
        static_cast<uint32_t>(Serial.parseInt());
      const bool accepted = bluetooth.classic().providePasskey(
        passkeyAddress.c_str(), passkey);
      Serial.printf("SPP_PASSKEY_PROVIDED accepted=%u passkey=%06u\n",
        accepted ? 1 : 0, static_cast<unsigned>(passkey));
      passkeyAddress = "";
    }
    else if (command == 'r' && bluetooth.initialized())
    {
      const bool cleared = bluetooth.classic().deleteAllBonds();
      bluetooth.end();
      const bool restarted = startSecureServer(
        EspBluedroidClassicSecurityIoCapability::DisplayOnly);
      Serial.printf(
        "SPP_PASSKEY_DISPLAY_RESTART cleared=%u restarted=%u\n",
        cleared ? 1 : 0, restarted ? 1 : 0);
    }
  }
  bluetooth.update();
  delay(1);
}
