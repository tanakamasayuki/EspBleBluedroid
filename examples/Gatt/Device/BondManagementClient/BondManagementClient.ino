// en: BondManagementClient - connect to a Bond Management Service (0x181E), read
//     the Bond Management Feature bit field, and write the Bond Management
//     Control Point op code "Delete bond of requesting device (LE)" (0x03) with
//     response.
// ja: BondManagementClient - Bond Management Service（0x181E）へ接続し、Bond
//     Management Feature bit fieldをRead、Bond Management Control Pointへ
//     「Delete bond of requesting device（LE）」（0x03）を応答ありWriteする。
#include <EspBleBluedroid.h>

static constexpr const char *BMS_SERVICE_UUID = "181e";
static constexpr const char *BOND_MANAGEMENT_CONTROL_POINT_UUID = "2aa4";
static constexpr const char *BOND_MANAGEMENT_FEATURE_UUID = "2aa5";

EspBleBluedroid bluetooth;
bool requested = false;

void setup()
{
  Serial.begin(115200);
  if (!bluetooth.begin())
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    requested = false;
    bluetooth.readCharacteristic(connection.id, BMS_SERVICE_UUID, BOND_MANAGEMENT_FEATURE_UUID);
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    if (!result.characteristicUuid.equalsIgnoreCase(BOND_MANAGEMENT_FEATURE_UUID) ||
        !result.success || result.value.length() < 3)
      return;
    uint32_t feature = 0;
    for (int i = 2; i >= 0; --i)
      feature = (feature << 8) | static_cast<uint8_t>(result.value[i]);
    Serial.printf("Bond Management Feature: 0x%06x\n", static_cast<unsigned>(feature));
    if (!requested)
    {
      requested = true;
      // en: Op code 0x03 = Delete bond of requesting device (LE transport only).
      // ja: op code 0x03 = Delete bond of requesting device（LE transportのみ）。
      const uint8_t request[1] = {0x03};
      bluetooth.writeCharacteristic(
        result.connectionId, BMS_SERVICE_UUID, BOND_MANAGEMENT_CONTROL_POINT_UUID, request, sizeof(request), true);
    }
  });
  bluetooth.onCharacteristicWritten([](const EspBleGattResult &result) {
    if (result.characteristicUuid.equalsIgnoreCase(BOND_MANAGEMENT_CONTROL_POINT_UUID) && result.success)
      Serial.println("Delete-bond op code sent");
  });

  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (result.connectable && result.advertisesService(BMS_SERVICE_UUID))
    {
      bluetooth.scanner().stop();
      bluetooth.connect(result);
    }
  });
  bluetooth.scanner().start();
}

void loop()
{
  bluetooth.update();
  delay(1);
}
