// en: JustWorksServer - a GATT server whose characteristic requires an encrypted link.
//     Pairing uses Just Works (no passkey, LE Secure Connections) + bonding, started
//     automatically on connection. Reading before encryption returns insufficient-
//     encryption, which prompts the OS to pair.
// ja: JustWorksServer - 暗号化されたlinkを要求するCharacteristicを持つGATT Server。
//     PairingはJust Works（passkeyなし、LE Secure Connections）+ Bondingで、接続時に自動開始する。
//     暗号化前にCharacteristicを読むとinsufficient-encryptionエラーになり、OS側のPairingが誘発される。
#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID = "be31dd60-5e70-4fd5-9003-736563757265";
static constexpr const char *CHARACTERISTIC_UUID = "be31dd61-5e70-4fd5-9003-736563757265";

EspBleBluedroid bluetooth;

EspBleGattService service;
EspBleGattCharacteristic characteristic;
void setup()
{
  Serial.begin(115200);

  EspBleGattCharacteristicConfig valueConfig;
  valueConfig.readable = true;
  valueConfig.writable = true;
  // en: require an encrypted link for read/write (enforced at the ATT layer)
  // ja: Read/Writeに暗号化linkを要求（ATT層で強制）
  valueConfig.encryptedRead = true;
  valueConfig.encryptedWrite = true;

  auto &gattServer = bluetooth.gattServer();
  service = gattServer.addService(SERVICE_UUID);
  characteristic = gattServer.addCharacteristic(service, CHARACTERISTIC_UUID, valueConfig);
  gattServer.setValue(characteristic, String("encrypted value"));
  gattServer.onWritten([](const EspBleGattWrite &write) {
    Serial.printf("Encrypted write: %s\n", write.value.c_str());
  });

  EspBleConfig config;
  config.deviceName = "Bluedroid Secure";
  config.security.enabled = true;       // en: enable security / ja: Security有効化
  config.security.bonding = true;       // en: store keys (auto-encrypt on reconnect) / ja: 鍵を保存（再接続時に自動暗号化）
  config.security.pairOnConnect = true; // en: start pairing on connection / ja: 接続と同時にPairingを開始
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  // en: On a peripheral-only device EspBleBluedroid delivers no BLE security
  //     event and no peripheral connect/disconnect callback (README). What the
  //     server can observe is the outcome: a successful write means the ATT layer
  //     accepted an encrypted link, and the bond list grows when keys are stored.
  //     loop() below prints bondCount() whenever it changes.
  // ja: Peripheral単体の機器では、EspBleBluedroidはBLEのsecurity eventも
  //     Peripheral側の接続・切断callbackも配送しない（README参照）。Serverが観測できるのは
  //     結果のほうで、Writeが成功したことは暗号化linkがATT層で受理された証拠になり、
  //     鍵が保存されればbond一覧が増える。下の loop() はbondCount()の変化を表示する。

  auto &advertising = bluetooth.advertising();
  advertising.setName("Bluedroid Secure");
  advertising.addServiceUuid(SERVICE_UUID);
  advertising.start();

  Serial.printf("Advertising. Stored bonds: %u\n",
    static_cast<unsigned>(bluetooth.bondCount()));
  Serial.println("Send 'c' while disconnected to clear all bonds.");
}

// en: The last reported bond count, so a change can be printed once.
// ja: 直前に表示したbond数。変化したときだけ表示するために保持する。
size_t lastBondCount = 0;

void loop()
{
  // en: Bond storage is device-wide, so it works without a connection snapshot.
  //     A new entry appearing here is the peripheral-side evidence that pairing
  //     and bonding completed.
  // ja: bondの保存は機器単位なので、connection snapshotがなくても参照できる。
  //     ここに項目が増えることが、PairingとBondingが完了したというPeripheral側の証拠になる。
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

  // en: On 'c', delete all bonds (allowed only while disconnected).
  // ja: Serialで 'c' を受けたら全Bondを削除（切断中のみ許可される）。
  if (Serial.available() > 0 && Serial.read() == 'c')
  {
    Serial.printf(
      "Clear bonds: %s, remaining=%u\n",
      bluetooth.deleteAllBonds() ? "success" : bluetooth.lastErrorName(),
      static_cast<unsigned>(bluetooth.bondCount()));
  }

  bluetooth.update();
  delay(1);
}
