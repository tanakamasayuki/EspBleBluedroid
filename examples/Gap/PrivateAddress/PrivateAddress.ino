// en: Advertise without exposing the factory public BLE address.
//       RandomStatic      = one fixed random identity
//       ResolvablePrivate = a controller-rotated RPA; bonded peers resolve it
// ja: 工場出荷のpublic BLE addressを公開せずにadvertiseする。
//       RandomStatic      = 固定random identity
//       ResolvablePrivate = controllerが回転するRPA。bonded peerは解決可能
#include <EspBleBluedroid.h>

// en: false works standalone; true is useful together with bonding.
// ja: falseは単体で利用可能。trueはbondingと組み合わせて利用する。
static constexpr bool USE_RESOLVABLE_PRIVATE_ADDRESS = false;

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "Bluedroid Private Address";
  if (USE_RESOLVABLE_PRIVATE_ADDRESS)
  {
    // en: A bonded peer receives the IRK needed to recognise later RPAs.
    // ja: bonded peerは後のRPAを同じ機器と判断するためのIRKを受け取る。
    config.ownAddressType = EspBleOwnAddressType::ResolvablePrivate;
    config.security.enabled = true;
    config.security.bonding = true;
  }
  else
  {
    // en: Hides the public address, but the fixed value remains trackable.
    // ja: public addressは隠れるが、固定値なのでその値による追跡は可能。
    config.ownAddressType = EspBleOwnAddressType::RandomStatic;
  }

  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE init failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  auto &advertising = bluetooth.advertising();
  advertising.setName("Bluedroid Private Address");
  advertising.addServiceUuid("180f");
  if (!advertising.start())
  {
    Serial.printf("Advertising failed: %s (%s)\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  const String address = bluetooth.localAddress();
  Serial.printf(
    "Advertising with %s address; current=%s type=%u\n",
    USE_RESOLVABLE_PRIVATE_ADDRESS ? "a resolvable private" : "a random static",
    address.isEmpty() ? "(controller-managed)" : address.c_str(),
    static_cast<unsigned>(bluetooth.localAddressType()));
}

void loop()
{
  bluetooth.update();
  delay(1);
}
