// en: Restrict BLE connection requests in the controller before they reach the
//     application. Replace ALLOWED_CENTRAL with the central's identity address.
// ja: BLE接続要求をapplicationへ届く前にcontrollerで制限する。
//     ALLOWED_CENTRALを接続許可するCentralのidentity addressへ置き換える。
#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID =
  "5266f727-49d7-4eaf-a6f1-6163636570";
static constexpr const char *ALLOWED_CENTRAL = "aa:bb:cc:dd:ee:ff";

EspBleBluedroid bluetooth;

bool startAdvertising(EspBleAdvertisingFilterPolicy policy)
{
  auto &advertising = bluetooth.advertising();
  advertising.stop();
  advertising.setFilterPolicy(policy);
  if (!advertising.start())
  {
    Serial.printf("Advertising failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return false;
  }
  return true;
}

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "Bluedroid Accept List";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE init failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  // en: Public/Random must match the allowed peer's identity address type.
  //     Adding the same pair again is harmless and does not consume a slot.
  // ja: Public/Randomは許可するpeerのidentity address種別に合わせる。
  //     同じ組を再追加してもslotは消費しない。
  if (!bluetooth.addToAcceptList(
        ALLOWED_CENTRAL, EspBleAddressType::Public))
  {
    Serial.printf("Accept list failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  auto &advertising = bluetooth.advertising();
  advertising.setName("Bluedroid Accept List");
  advertising.addServiceUuid(SERVICE_UUID);

  // en: Anyone can still scan this device, but only listed peers may connect.
  // ja: scanは誰にでも許可し、接続だけを登録済みpeerへ制限する。
  if (!startAdvertising(
        EspBleAdvertisingFilterPolicy::ConnectionFromAcceptList))
  {
    return;
  }
  Serial.printf(
    "Restricted advertising. Only %s may connect. Send o/r to change policy.\n",
    ALLOWED_CENTRAL);
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'o' || command == 'r')
    {
      const bool open = command == 'o';
      if (startAdvertising(
            open ? EspBleAdvertisingFilterPolicy::Any
                 : EspBleAdvertisingFilterPolicy::ConnectionFromAcceptList))
      {
        Serial.printf("Policy: %s (accept list has %u entries)\n",
          open ? "open" : "restricted",
          static_cast<unsigned>(bluetooth.acceptListCount()));
      }
    }
  }

  bluetooth.update();
  delay(1);
}
