// en: StaticPasskeyClient - central-side counterpart of StaticPasskeyServer: the keyboard
//     side (KeyboardOnly) that "types" the passkey. The passkey is fixed in the sketch
//     and handed to the stack up front, so nothing is typed at runtime; see
//     Security/RuntimePasskeyClient for the form where the user enters it. After
//     MITM-authenticated pairing it reads the protected characteristic.
// ja: StaticPasskeyClient - StaticPasskeyServerのCentral側。passkeyを「入力する」側
//     （KeyboardOnly）。passkeyはsketchに固定して事前にスタックへ渡すため、実行時の入力は
//     伴わない。利用者が打ち込む形は Security/RuntimePasskeyClient を参照。
//     MITM認証Pairing完了後、保護されたCharacteristicをReadする。
#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID = "9f78d810-802e-43e7-9003-706173736b79";
static constexpr const char *CHARACTERISTIC_UUID = "9f78d811-802e-43e7-9003-706173736b79";

// en: Must match the passkey displayed by StaticPasskeyServer.
// ja: StaticPasskeyServerが表示するpasskeyと一致させること。
static constexpr uint32_t STATIC_PASSKEY = 438209;

EspBleBluedroid bluetooth;
bool connectionRequested = false;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "Bluedroid Passkey Client";
  config.security.enabled = true;
  config.security.bonding = true;
  config.security.mitm = true;
  // en: this side "types" the passkey / ja: passkeyを「入力する」側
  config.security.ioCapability = EspBleSecurityIoCapability::KeyboardOnly;
  config.security.staticPasskeyEnabled = true;
  config.security.staticPasskey = STATIC_PASSKEY;
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    // en: Start pairing explicitly; completion arrives via onSecurityChanged().
    // ja: 明示的にPairingを開始する。完了は onSecurityChanged() で届く。
    if (!bluetooth.requestSecurity(connection.id))
    {
      Serial.printf("Security request failed: %s\n", bluetooth.lastErrorDetail().c_str());
    }
  });
  bluetooth.onSecurityChanged([](const EspBleSecurityChanged &event) {
    Serial.printf(
      "Security %s: encrypted=%u authenticated=%u bonded=%u\n",
      event.success ? "established" : "failed",
      event.connection.encrypted ? 1 : 0,
      event.connection.authenticated ? 1 : 0,
      event.connection.bonded ? 1 : 0);
    if (event.success)
    {
      // en: The characteristic requires an authenticated link, so this only
      //     succeeds after MITM pairing completed.
      // ja: CharacteristicはMITM認証済みlinkを要求するため、MITM Pairing完了後にのみ成功する。
      bluetooth.discoverCharacteristic(event.connection.id, SERVICE_UUID, CHARACTERISTIC_UUID);
    }
  });
  bluetooth.onCharacteristicDiscovered([](const EspBleGattResult &result) {
    if (result.success)
    {
      bluetooth.readCharacteristic(result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID);
    }
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    if (result.success)
    {
      Serial.printf("Protected value: %s\n", result.value.c_str());
    }
    else
    {
      Serial.printf("Read failed: %s\n", result.detail.c_str());
    }
  });
  bluetooth.onDisconnected([](const EspBleConnection &) {
    connectionRequested = false;
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &scanResult) {
    if (connectionRequested || !scanResult.advertisesService(SERVICE_UUID))
    {
      return;
    }
    bluetooth.scanner().stop();
    connectionRequested = bluetooth.connect(scanResult);
  });

  EspBleScanConfig scanConfig;
  scanConfig.active = true;
  bluetooth.scanner().start(scanConfig);

  Serial.println("Send 'c' while disconnected to clear all bonds.");
}

void loop()
{
  // en: On 'c', delete all bonds (allowed only while disconnected).
  // ja: 'c' で全Bondを削除（切断中のみ許可）。
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
