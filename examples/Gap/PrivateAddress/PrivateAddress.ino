// en: PrivateAddress - advertise with a private address instead of the factory public
//     address, so passers-by cannot track this device by its address.
//       RandomStatic      = a fixed random address (hides the public one, never rotates)
//       ResolvablePrivate = an RPA the controller rotates; a bonded peer resolves it
//                           with the IRK exchanged at bonding, everyone else sees a
//                           changing address
//     Set USE_RESOLVABLE_PRIVATE_ADDRESS to switch. Observe with Gap/Scan or
//     Info/ScanDump: the address type shows as Random either way. This device is
//     a peripheral, and EspBleBluedroid delivers no peripheral-side connection
//     callbacks, so the connection is observed from the scanner side (README).
// ja: PrivateAddress - 工場出荷のpublic addressの代わりにprivate addressでadvertiseし、
//     周囲からアドレスで追跡されないようにする。
//       RandomStatic      = 固定random address（public addressを隠すが回転はしない）
//       ResolvablePrivate = controllerが回転させるRPA。bonded peerはbonding時に交換した
//                           IRKで解決でき、それ以外からは変化するアドレスに見える
//     USE_RESOLVABLE_PRIVATE_ADDRESS で切り替える。Gap/ScanやInfo/ScanDumpで受信すると
//     どちらもaddress typeはRandomになる。この機器はPeripheralで、EspBleBluedroidは
//     Peripheral側の接続callbackを配送しないため、接続の確認はscanner側で行う（README参照）。
#include <EspBleBluedroid.h>

// en: false = RandomStatic (works standalone), true = RPA (requires bonding).
// ja: false = RandomStatic（単体で動く）、true = RPA（bondingが前提）。
static constexpr bool USE_RESOLVABLE_PRIVATE_ADDRESS = false;

EspBleBluedroid bluetooth;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "Bluedroid Private Address";

  if (USE_RESOLVABLE_PRIVATE_ADDRESS)
  {
    // en: An RPA is only useful together with bonding: the peer needs the IRK to
    //     recognise this device across a rotation. Without security enabled, a
    //     peer cannot follow the address change and reconnects fail.
    // ja: RPAはbonding併用でのみ意味を持つ。回転後も同一機器と分かるにはpeerにIRKが
    //     必要なため。securityなしでは相手がアドレス変化を追えず再接続できない。
    config.ownAddressType = EspBleOwnAddressType::ResolvablePrivate;
    config.security.enabled = true;
    config.security.bonding = true;
  }
  else
  {
    // en: A fixed random address. It hides the factory address but, because it
    //     never changes, it can still be used to track this device.
    // ja: 固定のrandom address。工場出荷アドレスは隠せるが、変化しないので
    //     このアドレス自体での追跡は防げない。
    config.ownAddressType = EspBleOwnAddressType::RandomStatic;
  }

  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE init failed: %s (%s)\n", bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  auto &advertising = bluetooth.advertising();
  advertising.setName("Bluedroid Private Address");
  if (!advertising.start())
  {
    Serial.printf("Advertising failed: %s (%s)\n", bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }
  // en: localAddress() returns the on-air Random Static address. For an RPA it
  //     returns an empty String: the original ESP32 controller generates the
  //     current RPA internally and its supported GAP API does not expose it.
  // ja: localAddress() は電波に乗っているRandom Static addressを返す。RPAでは空文字列
  //     になる。無印ESP32のcontrollerは現在のRPAを内部生成し、対応するGAP APIから
  //     その値を取得できないため。
  const String address = bluetooth.localAddress();
  Serial.printf(
    "Advertising with %s address; current=%s type=%u\n",
    USE_RESOLVABLE_PRIVATE_ADDRESS ? "a resolvable private (rotating)" : "a random static",
    address.isEmpty() ? "(controller-managed)" : address.c_str(),
    static_cast<unsigned>(bluetooth.localAddressType()));
}

void loop()
{
  bluetooth.update();
  delay(1);
}
