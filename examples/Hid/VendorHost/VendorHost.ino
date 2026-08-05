// en: VendorHost - drive a vendor-defined HID device as a HID host: discover it, receive
//     its Input reports, and write its Output and Feature reports. Pairs with the
//     Hid/VendorDevice example.
// ja: VendorHost - HID Hostとしてvendor定義HIDデバイスを駆動する。Discoveryし、Input Reportを
//     受け取り、OutputとFeature Reportを書き込む。Hid/VendorDevice exampleとペア。
#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;
EspBleConnectionId connectionId = 0;

void setup()
{
  Serial.begin(115200);

  bluetooth.hidHost().onDiscovered([](const EspBleHidKeyboardHostDiscovery &result) {
    // en: result.detail says what failed; the library's lastError has moved on by now.
    // ja: 失敗理由は result.detail。この時点でライブラリのlastErrorは既に別の値。
    Serial.printf("HID discovery: %s\n", result.success ? "ready" : result.detail.c_str());
  });
  // en: A vendor report is bytes the library does not interpret, so the event carries the
  //     report as it arrived.
  // ja: vendor reportはライブラリが解釈しないbyte列なので、eventは届いたままのreportを載せる。
  bluetooth.hidHost().onVendorInput([](const EspBleHidVendorInputEvent &event) {
    Serial.printf("Vendor Input report=%u length=%u data=", event.reportId,
      static_cast<unsigned>(event.rawLength));
    for (size_t index = 0; index < event.rawLength; ++index)
      Serial.printf("%s%02x", index == 0 ? "" : " ", event.rawData[index]);
    Serial.println();
  });

  EspBleConfig config;
  config.deviceName = "Bluedroid Vendor Host";
  // en: A report has to fit in one ATT payload, which is MTU - 3. The default 63-byte
  //     vendor report needs more than the 23-byte default MTU.
  // ja: reportは1つのATT payload（MTU - 3）に収まる必要がある。既定63 byteのvendor reportは
  //     既定MTU 23では足りない。
  config.preferredMtu = 100;
  config.security.enabled = true;
  config.security.bonding = true;
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }
  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
  });
  bluetooth.onSecurityChanged([](const EspBleSecurityChanged &event) {
    if (event.success) bluetooth.hidHost().discover(event.connection.id);
  });
  bluetooth.onDisconnected([](const EspBleConnection &) { connectionId = 0; });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionId != 0 || !result.advertisesService("1812")) return;
    bluetooth.scanner().stop();
    bluetooth.connect(result);
  });
  bluetooth.scanner().start();
  Serial.println("Send 'o' for Output or 'f' for Feature after discovery.");
}

void loop()
{
  // en: Wait for onDiscovered() before writing: the Output and Feature handles come from
  //     the Report Reference descriptors that discovery reads, and ready() is the question
  //     to ask if the sketch does not track discovery itself.
  // ja: 書込みは onDiscovered() の後で。OutputとFeatureのhandleはDiscoveryが読む
  //     Report Reference descriptorから得られる。Discoveryを自前で追わないなら ready() を見る。
  if (Serial.available() > 0 && connectionId != 0)
  {
    const char command = Serial.read();
    if (command == 'o')
    {
      const uint8_t report[] = {'O', 'U', 'T', 3, 4, 5, 6, 7};
      Serial.printf("Output: %s\n", bluetooth.hidHost().sendVendorOutput(
        connectionId, report, sizeof(report)) ? "sent" : "failed");
    }
    else if (command == 'f')
    {
      const uint8_t report[] = {'F', 'E', 'A', 'T', 4, 5, 6, 7};
      Serial.printf("Feature: %s\n", bluetooth.hidHost().sendVendorFeature(
        connectionId, report, sizeof(report)) ? "sent" : "failed");
    }
  }
  bluetooth.update();
  delay(1);
}
