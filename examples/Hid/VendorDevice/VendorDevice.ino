// en: VendorDevice - a vendor-defined HID device: an Input, an Output and a Feature
//     report of a caller-chosen size, with bytes the library does not interpret.
//     Unlike the fixed profiles it is bidirectional, so the host can write to it.
// ja: VendorDevice - vendor定義のHIDデバイス。任意サイズのInput・Output・Feature Reportを持ち、
//     中身のbyte列はライブラリが解釈しない。固定profileと違い双方向で、Hostから書き込める。
#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;
uint8_t counter = 0;

static void printReport(const char *label, const EspBleHidVendorReport &report)
{
  Serial.printf("%s type=%u length=%u data=", label, report.reportType,
    static_cast<unsigned>(report.length));
  for (size_t index = 0; index < report.length; ++index)
    Serial.printf("%s%02x", index == 0 ? "" : " ", report.data[index]);
  Serial.println();
}

void setup()
{
  Serial.begin(115200);

  EspBleHidVendorConfig vendorConfig;
  // en: The size is patched into the Report Descriptor, so it is fixed for every
  //     report: sendInput() refuses any other length rather than padding it.
  // ja: サイズはReport Descriptorへ埋め込まれるので全Reportで固定。sendInput() は他の長さを
  //     padding せず拒否する。
  vendorConfig.reportSize = 8;
  if (!bluetooth.hidVendor().configure(vendorConfig))
  {
    Serial.printf("Vendor HID configuration failed: %s\n",
      bluetooth.lastErrorDetail().c_str());
    return;
  }
  // en: The host writes these; the bytes arrive exactly as sent. Both callbacks are
  //     dispatched from update(), so they run in the loop() task.
  // ja: これらはHostが書き込む。byte列は送られたままの形で届く。どちらのcallbackも update()
  //     から配送されるので loop() タスクで動く。
  bluetooth.hidVendor().onOutputReport([](const EspBleHidVendorReport &report) {
    printReport("Output", report);
  });
  bluetooth.hidVendor().onFeatureReport([](const EspBleHidVendorReport &report) {
    printReport("Feature", report);
  });

  EspBleConfig config;
  config.deviceName = "Bluedroid Vendor HID";
  // en: An ATT payload is MTU - 3, so a report larger than 20 bytes needs a raised
  //     MTU. The host has to accept it; this is only what the device asks for.
  // ja: ATT payloadは MTU - 3 なので、20byteを超えるReportにはMTUの引き上げが必要。
  //     Host側が受け入れる必要があり、ここで指定するのはデバイス側の希望値。
  config.preferredMtu = 100;
  config.security.enabled = true;
  config.security.bonding = true;
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onDisconnected([](const EspBleConnection &) {
    bluetooth.advertising().start();
  });
  bluetooth.advertising().setName(config.deviceName);
  bluetooth.advertising().start();

  Serial.println("Send 'i' to send an 8-byte Vendor Input Report.");
}

void loop()
{
  if (Serial.available() > 0 && Serial.read() == 'i')
  {
    const uint8_t report[] = {'E', 'S', 'P', counter++, 4, 5, 6, 7};
    Serial.printf("Input: %s\n",
      bluetooth.hidVendor().sendInput(report, sizeof(report)) ? "sent" : "failed");
  }

  // en: Output and Feature report events are delivered from this update().
  // ja: Output・Feature Reportイベントはこの update() から配送される。
  bluetooth.update();
  delay(1);
}
