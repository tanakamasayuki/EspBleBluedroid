#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID =
  "f22f68c8-2a62-5b78-a16c-0d5feacd9111";

EspBleBluedroid bluetooth;

static bool beginIdentity(EspBleOwnAddressType addressType)
{
  EspBleConfig config;
  config.deviceName = "Bluedroid Private Identity";
  config.ownAddressType = addressType;
  if (addressType == EspBleOwnAddressType::ResolvablePrivate)
  {
    config.security.enabled = true;
    config.security.bonding = true;
    config.security.pairOnConnect = false;
  }
  if (!bluetooth.begin(config)) return false;

  auto &advertising = bluetooth.advertising();
  advertising.clear();
  advertising.setName("Bluedroid Private Identity");
  advertising.addServiceUuid(SERVICE_UUID);
  advertising.data().setTxPowerIncluded(true);
  return advertising.start();
}

static bool restartAdvertising()
{
  return bluetooth.advertising().stop() &&
    bluetooth.advertising().start();
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  const bool prebeginAddressEmpty = bluetooth.localAddress().isEmpty();
  const bool prebeginPowerUnknown = bluetooth.txPower() == INT8_MIN;

  if (!beginIdentity(EspBleOwnAddressType::RandomStatic))
  {
    Serial.printf("BEGIN_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  Serial.println("IDENTITY_READY");
  Serial.printf(
    "PREBEGIN address_empty=%u tx_unknown=%u\n",
    prebeginAddressEmpty ? 1 : 0,
    prebeginPowerUnknown ? 1 : 0);
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'a')
    {
      const String address = bluetooth.localAddress();
      Serial.printf(
        "LOCAL address=%s type=%u\n",
        address.isEmpty() ? "-" : address.c_str(),
        static_cast<unsigned>(bluetooth.localAddressType()));
    }
    else if (command == 'l' || command == 'h')
    {
      const int8_t requested = command == 'l' ? -12 : 9;
      const bool accepted = bluetooth.setTxPower(requested);
      const bool restarted = accepted && restartAdvertising();
      Serial.printf(
        "POWER accepted=%u applied=%d restarted=%u\n",
        accepted ? 1 : 0,
        static_cast<int>(bluetooth.txPower()),
        restarted ? 1 : 0);
    }
    else if (command == 'r')
    {
      bluetooth.end();
      const bool ready =
        beginIdentity(EspBleOwnAddressType::ResolvablePrivate);
      Serial.printf(
        "RPA_READY %u error=%s\n",
        ready ? 1 : 0, bluetooth.lastErrorName());
    }
  }

  bluetooth.update();
  delay(1);
}
