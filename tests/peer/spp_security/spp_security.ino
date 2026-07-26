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

size_t clearClassicBonds()
{
  const int count = esp_bt_gap_get_bond_device_num();
  if (count <= 0) return 0;
  esp_bd_addr_t *addresses =
    new esp_bd_addr_t[static_cast<size_t>(count)];
  int actual = count;
  if (esp_bt_gap_get_bond_device_list(&actual, addresses) != ESP_OK)
  {
    delete[] addresses;
    return 0;
  }
  size_t removed = 0;
  for (int index = 0; index < actual; ++index)
  {
    if (esp_bt_gap_remove_bond_device(addresses[index]) == ESP_OK) ++removed;
  }
  delete[] addresses;
  return removed;
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
        "SPP_SECURITY_CONNECTED id=%u authenticated=%u encrypted=%u\n",
        static_cast<unsigned>(session.id),
        session.authenticated ? 1 : 0, session.encrypted ? 1 : 0);
    });
  bluetooth.classic().spp().onData([](const EspBluedroidSppData &event) {
    bluetooth.classic().spp().write(event.sessionId, event.value);
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
  Serial.printf("SPP_SECURITY_BONDS_CLEARED %u\n",
    static_cast<unsigned>(clearClassicBonds()));

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
  }
  bluetooth.update();
  delay(1);
}
