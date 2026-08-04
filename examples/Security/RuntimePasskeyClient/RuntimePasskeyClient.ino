// en: RuntimePasskeyClient - the input side of Passkey Entry. It connects, pairing
//     starts, and the stack asks for a passkey; the sketch answers with
//     providePasskey() using a value typed into the serial monitor. That models a
//     user reading the peer's display and typing what they see.
// ja: RuntimePasskeyClient - Passkey Entryの入力側。接続するとPairingが始まり、
//     スタックがpasskeyを要求する。sketchはSerialから入力された値を
//     providePasskey() で答える。相手の表示を読んで人間が打ち込む場面の再現。
#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID = "9f78d820-802e-43e7-9003-706173736b79";
static constexpr const char *CHARACTERISTIC_UUID = "9f78d821-802e-43e7-9003-706173736b79";

EspBleBluedroid bluetooth;
bool connectionRequested = false;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "Bluedroid Runtime Passkey Client";
  config.security.enabled = true;
  config.security.bonding = true;
  config.security.mitm = true;
  // en: this side types the passkey / ja: passkeyを入力する側
  config.security.ioCapability = EspBleSecurityIoCapability::KeyboardOnly;
  // en: No static passkey, so the stack waits for providePasskey() below.
  // ja: 静的passkeyを設定しないので、スタックは下の providePasskey() を待つ。
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    // en: pairOnConnect is on by default, so pairing has already begun here.
    // ja: pairOnConnect は既定で有効なので、この時点でPairingは始まっている。
    Serial.printf(
      "Connected id=%u. Type p<passkey> (e.g. p123456) shown on the peer.\n",
      static_cast<unsigned>(connection.id));
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
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'p')
    {
      // en: The pairing is blocked waiting for this; it gives up after 30 s.
      // ja: Pairingはこれを待って止まっている。30秒で打ち切られる。
      const uint32_t passkey = static_cast<uint32_t>(Serial.parseInt());
      Serial.printf(
        "Passkey %06u %s\n",
        static_cast<unsigned>(passkey),
        bluetooth.providePasskey(passkey) ? "provided" : bluetooth.lastErrorName());
    }
    // en: On 'c', delete all bonds (allowed only while disconnected).
    // ja: 'c' で全Bondを削除（切断中のみ許可）。
    else if (command == 'c')
    {
      Serial.printf(
        "Clear bonds: %s, remaining=%u\n",
        bluetooth.deleteAllBonds() ? "success" : bluetooth.lastErrorName(),
        static_cast<unsigned>(bluetooth.bondCount()));
    }
  }

  bluetooth.update();
  delay(1);
}
