// en: StaticPasskeyServer - a GATT server requiring MITM-authenticated pairing with a
//     static 6-digit passkey. This board is the display side (DisplayOnly): it prints
//     the passkey and the connecting central types it. The characteristic requires an
//     authenticated link, so Just Works pairing cannot access it.
// ja: StaticPasskeyServer - 静的6桁passkeyによるMITM認証Pairingを要求するGATT Server。
//     ボードは表示側（DisplayOnly）で、passkeyをSerialへ表示する。接続するCentralがそれを入力する。
//     CharacteristicはMITM認証済みlinkを要求するため、Just Works Pairingではアクセスできない。
#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID = "9f78d810-802e-43e7-9003-706173736b79";
static constexpr const char *CHARACTERISTIC_UUID = "9f78d811-802e-43e7-9003-706173736b79";

// en: Example-only fixed value. Production devices should provision a unique passkey.
// ja: example用の固定値。製品ではデバイスごとに安全にprovisioningすること。
static constexpr uint32_t STATIC_PASSKEY = 438209;

EspBleBluedroid bluetooth;

EspBleGattService service;
EspBleGattCharacteristic characteristic;
void setup()
{
  Serial.begin(115200);

  EspBleGattCharacteristicConfig valueConfig;
  valueConfig.readable = true;
  valueConfig.writable = true;
  // en: require a MITM-authenticated link / ja: MITM認証済みlinkを要求
  valueConfig.authenticatedRead = true;
  valueConfig.authenticatedWrite = true;

  auto &gattServer = bluetooth.gattServer();
  service = gattServer.addService(SERVICE_UUID);
  characteristic = gattServer.addCharacteristic(service, CHARACTERISTIC_UUID, valueConfig);
  gattServer.setValue(characteristic, String("MITM protected value"));

  EspBleConfig config;
  config.deviceName = "Bluedroid Passkey";
  config.security.enabled = true;
  config.security.bonding = true;
  config.security.mitm = true; // en: require MITM protection (passkey auth) / ja: MITM保護（passkey認証）を要求
  // en: display side / ja: 表示側
  config.security.ioCapability = EspBleSecurityIoCapability::DisplayOnly;
  config.security.staticPasskeyEnabled = true;
  config.security.staticPasskey = STATIC_PASSKEY;
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  // en: The passkey is a compile-time constant here, so it can be printed now.
  //     A peripheral-only device receives no onPasskeyDisplayed() and no BLE
  //     security event from EspBleBluedroid (README), which is exactly why the
  //     static form is the one that works on this side: the value to display does
  //     not have to come from the stack.
  // ja: ここではpasskeyがコンパイル時定数なので、この場で表示できる。Peripheral単体の
  //     機器にはEspBleBluedroidから onPasskeyDisplayed() もBLEのsecurity eventも
  //     届かない（README参照）。表示すべき値をstackから受け取る必要がない静的方式が、
  //     この側で成立する方式である理由がこれ。
  Serial.printf("Enter passkey %06u on the peer.\n",
    static_cast<unsigned>(STATIC_PASSKEY));

  auto &advertising = bluetooth.advertising();
  advertising.setName("Bluedroid Passkey");
  advertising.addServiceUuid(SERVICE_UUID);
  advertising.start();
  Serial.printf("Advertising. Stored bonds: %u\n",
    static_cast<unsigned>(bluetooth.bondCount()));
}

// en: The last reported bond count, so a change can be printed once.
// ja: 直前に表示したbond数。変化したときだけ表示するために保持する。
size_t lastBondCount = 0;

void loop()
{
  // en: Bond storage is device-wide, so a new entry is the peripheral-side
  //     evidence that MITM-authenticated pairing completed.
  // ja: bondの保存は機器単位なので、項目が増えることがMITM認証Pairingの完了を示す
  //     Peripheral側の証拠になる。
  const size_t bonds = bluetooth.bondCount();
  if (bonds != lastBondCount)
  {
    lastBondCount = bonds;
    EspBleBond bond;
    if (bonds > 0 && bluetooth.bond(bonds - 1, bond))
    {
      Serial.printf("Bonded with %s (total %u)\n",
        bond.peerAddress.c_str(), static_cast<unsigned>(bonds));
    }
    else
    {
      Serial.printf("Bonds: %u\n", static_cast<unsigned>(bonds));
    }
  }

  bluetooth.update();
  delay(1);
}
