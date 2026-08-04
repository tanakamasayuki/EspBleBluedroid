// en: JustWorksClient - central-side counterpart of JustWorksServer. It connects,
//     pairs with LE Secure Connections Just Works (no passkey) and bonding, then
//     reads the characteristic that requires an encrypted link. Just Works gives
//     encryption without MITM protection, so authenticated=0 on success.
// ja: JustWorksClient - JustWorksServerのCentral側。接続してLE Secure Connectionsの
//     Just Works（passkeyなし）+ Bondingでpairingし、暗号化linkを要求する
//     CharacteristicをReadする。Just WorksはMITM保護を伴わない暗号化なので、
//     成功しても authenticated=0 になる。
#include <EspBleBluedroid.h>

// en: The UUIDs published by the Security/JustWorksServer example.
// ja: Security/JustWorksServer exampleが公開するUUID。
static constexpr const char *SERVICE_UUID = "be31dd60-5e70-4fd5-9003-736563757265";
static constexpr const char *CHARACTERISTIC_UUID = "be31dd61-5e70-4fd5-9003-736563757265";

EspBleBluedroid bluetooth;
bool connectionRequested = false;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "Bluedroid Secure Central";
  config.security.enabled = true;
  config.security.bonding = true;
  // en: pairOnConnect starts pairing as soon as the link comes up. With
  //     ioCapability left at None on both sides, Just Works is what gets chosen.
  // ja: pairOnConnectはlink成立と同時にpairingを開始する。両側の ioCapability が
  //     None のままなら、選ばれる方式はJust Worksになる。
  config.security.pairOnConnect = true;
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    Serial.printf("Connected: %u\n", static_cast<unsigned>(connection.id));
  });
  // en: Security completion is delivered from update(), never from the stack task.
  // ja: Securityの完了は stack task ではなく update() から配送される。
  bluetooth.onSecurityChanged([](const EspBleSecurityChanged &event) {
    Serial.printf("Security: success=%u encrypted=%u bonded=%u key=%u\n",
      event.success ? 1 : 0, event.connection.encrypted ? 1 : 0,
      event.connection.bonded ? 1 : 0,
      event.connection.encryptionKeySize);
    EspBleBond firstBond;
    if (bluetooth.bondCount() > 0 && bluetooth.bond(0, firstBond))
    {
      Serial.printf("Stored bond: %s\n", firstBond.peerAddress.c_str());
    }
    if (event.success)
    {
      // en: Reading before encryption fails with an ATT security error, so the
      //     read is issued only once security is established.
      // ja: 暗号化前のReadはATTのsecurity errorになるため、Security確立後にReadする。
      bluetooth.discoverCharacteristic(
        event.connection.id, SERVICE_UUID, CHARACTERISTIC_UUID);
    }
  });
  bluetooth.onCharacteristicDiscovered([](const EspBleGattResult &result) {
    if (result.success)
    {
      bluetooth.readCharacteristic(
        result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID);
    }
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    if (result.success)
    {
      Serial.printf("Encrypted value: %s\n", result.value.c_str());
    }
    else
    {
      Serial.printf("Read failed: %s\n", result.detail.c_str());
    }
  });
  bluetooth.onDisconnected([](const EspBleConnection &) {
    connectionRequested = false;
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested ||
        !result.advertisesService(SERVICE_UUID)) return;
    bluetooth.scanner().stop();
    connectionRequested = bluetooth.connect(result);
  });
  bluetooth.scanner().start();

  Serial.println("Send 'c' while disconnected to clear all bonds.");
}

void loop()
{
  // en: Bond deletion waits for Bluedroid's persistent store, so do it while
  //     disconnected rather than during an active link.
  // ja: Bond削除はBluedroidの永続storeを待つため、接続中ではなく切断中に行う。
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
