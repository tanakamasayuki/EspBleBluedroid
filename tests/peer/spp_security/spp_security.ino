#include <EspBleBluedroid.h>
#include <esp_bt_device.h>
#include <esp_gap_bt_api.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
String comparisonAddress;
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

void initializeBluetooth()
{
  bluetooth.classic().onNumericComparisonRequested(
    [](const EspBluedroidClassicNumericComparison &event) {
      comparisonAddress = event.peerAddress;
      Serial.printf(
        "SPP_SECURITY_COMPARE address=%s value=%06u context=%s\n",
        event.peerAddress.c_str(), static_cast<unsigned>(event.value),
        contextName());
    });
  bluetooth.classic().onSecurityChanged(
    [](const EspBluedroidClassicSecurityChanged &event) {
      Serial.printf(
        "SPP_SECURITY_CHANGED address=%s success=%u status=%d context=%s\n",
        event.peerAddress.c_str(), event.success ? 1 : 0, event.status,
        contextName());
    });
  bluetooth.classic().spp().onServerStarted([]() {
    Serial.printf("SPP_SECURITY_READY address=%s\n",
      localAddress().c_str());
  });
  bluetooth.classic().spp().onConnected(
    [](const EspBluedroidSppSession &session) {
      Serial.printf(
        "SPP_SECURITY_CONNECTED id=%u authenticated=%u encrypted=%u "
        "incoming=%u\n",
        static_cast<unsigned>(session.id),
        session.authenticated ? 1 : 0, session.encrypted ? 1 : 0,
        session.incoming ? 1 : 0);
    });
  bluetooth.classic().spp().onDisconnected(
    [](const EspBluedroidSppSession &session) {
      Serial.printf("SPP_SECURITY_DISCONNECTED id=%u\n",
        static_cast<unsigned>(session.id));
    });
  bluetooth.classic().spp().onData([](const EspBluedroidSppData &event) {
    bluetooth.classic().spp().write(event.sessionId, event.value);
  });
  bluetooth.classic().spp().onConnectionFailed(
    [](const EspBluedroidSppConnectionFailure &failure) {
      Serial.printf(
        "SPP_SECURITY_CONNECTION_FAILED address=%s error=%u "
        "context=%s\n",
        failure.peerAddress.c_str(),
        static_cast<unsigned>(failure.error), contextName());
    });

  EspBleConfig config;
  config.deviceName = "EspBleBluedroid Secure SPP";
  config.classicSecurity.enabled = true;
  config.classicSecurity.ioCapability =
    EspBluedroidClassicSecurityIoCapability::DisplayYesNo;
  if (!bluetooth.begin(config))
  {
    Serial.printf("SPP_SECURITY_INIT_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }
  const bool cleared = bluetooth.classic().deleteAllBonds();
  Serial.printf("SPP_SECURITY_BONDS_CLEARED success=%u count=%u\n",
    cleared ? 1 : 0,
    static_cast<unsigned>(bluetooth.classic().bondCount()));

  EspBluedroidSppServerConfig server;
  server.serviceName = "EspBleBluedroid Secure";
  server.security =
    EspBluedroidSppSecurity::AuthenticatedEncrypted;
  if (!bluetooth.classic().spp().startServer(server))
  {
    Serial.printf("SPP_SECURITY_SERVER_FAILED %s\n",
      bluetooth.lastErrorName());
  }
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
    else if ((command == 'a' || command == 'r') &&
             !comparisonAddress.isEmpty())
    {
      const bool accepted =
        bluetooth.classic().confirmNumericComparison(
          comparisonAddress.c_str(), command == 'a');
      Serial.printf("SPP_SECURITY_CONFIRM accepted=%u reply=%u\n",
        command == 'a' ? 1 : 0, accepted ? 1 : 0);
      comparisonAddress = "";
    }
    else if (command == 'b')
    {
      EspBluedroidClassicBond first;
      const size_t count = bluetooth.classic().bondCount();
      const bool listed =
        count > 0 && bluetooth.classic().bond(0, first);
      Serial.printf("SPP_SECURITY_BONDS count=%u listed=%u address=%s\n",
        static_cast<unsigned>(count), listed ? 1 : 0,
        listed ? first.peerAddress.c_str() : "");
    }
    else if (command == 'd')
    {
      EspBluedroidClassicBond first;
      const bool deleted =
        bluetooth.classic().bond(0, first) &&
        bluetooth.classic().deleteBond(first);
      Serial.printf("SPP_SECURITY_BOND_DELETED success=%u count=%u\n",
        deleted ? 1 : 0,
        static_cast<unsigned>(bluetooth.classic().bondCount()));
    }
    else if (command == 'x')
    {
      Serial.printf("SPP_SECURITY_SERVER_STOPPED success=%u\n",
        bluetooth.classic().spp().stopServer() ? 1 : 0);
    }
    else if (command == 'c')
    {
      const String address = Serial.readStringUntil('\n');
      const bool accepted = bluetooth.classic().spp().connect(
        address.c_str(), 10000,
        EspBluedroidSppSecurity::AuthenticatedEncrypted);
      Serial.printf("SPP_SECURITY_CLIENT_CONNECT accepted=%u\n",
        accepted ? 1 : 0);
    }
  }
  bluetooth.update();
  delay(1);
}
