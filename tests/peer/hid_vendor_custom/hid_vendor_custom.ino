// The two HID device profiles whose payload the library does not interpret:
// hidVendor() (a fixed vendor-defined descriptor, Report ID 6) and hidCustom()
// (a descriptor the sketch supplies, Report ID 7 here).
//
// Both are bidirectional, which is what separates them from the profiles in
// peer/hid_composite: on top of an Input Report each declares an Output and a
// Feature report, so a host writes to the device and the sketch receives the raw
// bytes. They also share one HID service and one Report Map — the composed
// descriptor of the built-in profiles with the caller's descriptor appended —
// which is the part a report ID collision would break.
//
// The vendor reports are 40 bytes rather than the default 63, so the size patched
// into the descriptor is visible on the air (the default happens to equal the byte
// already in the table). 40 bytes still exceeds an ATT payload at the default MTU,
// so the host has to raise it, and the test asserts it did.
//
// The HID UUIDs are fixed by the specification, so isolation is by device name.

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *DEVICE_NAME = "Bluedroid HID 000e";
static constexpr uint8_t VENDOR_REPORT_SIZE = 40;
static constexpr uint8_t CUSTOM_REPORT_ID = 7;
static constexpr uint16_t CUSTOM_INPUT_SIZE = 4;
static constexpr uint16_t CUSTOM_OUTPUT_SIZE = 2;
static constexpr uint16_t CUSTOM_FEATURE_SIZE = 3;

// A vendor-defined Report Descriptor of the sketch's own making: Report ID 7 with
// a 4-byte input, a 2-byte output and a 3-byte feature report. The test carries
// the same bytes, because they are its input rather than the library's.
static const uint8_t CUSTOM_REPORT_MAP[] = {
  0x06, 0x01, 0xff,        // Usage Page (Vendor 0xff01)
  0x09, 0x01,              // Usage (1)
  0xa1, 0x01,              // Collection (Application)
  0x85, CUSTOM_REPORT_ID,  //   Report ID (7)
  0x15, 0x00,              //   Logical Minimum (0)
  0x26, 0xff, 0x00,        //   Logical Maximum (255)
  0x75, 0x08,              //   Report Size (8)
  0x09, 0x01,              //   Usage (1)
  0x95, CUSTOM_INPUT_SIZE, //   Report Count (4)
  0x81, 0x02,              //   Input (Data, Var, Abs)
  0x09, 0x02,              //   Usage (2)
  0x95, CUSTOM_OUTPUT_SIZE,//   Report Count (2)
  0x91, 0x02,              //   Output (Data, Var, Abs)
  0x09, 0x03,              //   Usage (3)
  0x95, CUSTOM_FEATURE_SIZE,// Report Count (3)
  0xb1, 0x02,              //   Feature (Data, Var, Abs)
  0xc0,                    // End Collection
};

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;
bool started = false;
bool configured = false;
uint8_t vendorReportSize = 0;
// The refusal of a report ID the vendor profile owns, kept for the 'k' command:
// anything printed from setup() can be lost while the other board is flashed.
bool conflictRefused = false;
const char *conflictError = "";
// The negotiated ATT MTU, which decides whether a 40-byte report fits in one
// packet at all. This is the device's own view of it, not the host's claim.
uint16_t negotiatedMtu = 23;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void printHex(const uint8_t *data, size_t length)
{
  for (size_t index = 0; index < length; ++index) Serial.printf("%02x", data[index]);
}

void printReport(const char *label, const EspBleHidVendorReport &report)
{
  // rawData/rawLength and data/length are the same bytes; printing the raw pair
  // for the length and the decoded pair for the bytes checks both spellings.
  Serial.printf("%s id=%u type=%u length=%u hex=", label, report.reportId,
    report.reportType, static_cast<unsigned>(report.rawLength));
  printHex(report.data, report.length);
  Serial.printf(" context=%s\n", contextName());
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();

  EspBleHidVendorConfig vendorConfig;
  vendorConfig.manufacturer = "EspBleBluedroid";
  vendorConfig.reportSize = VENDOR_REPORT_SIZE;
  configured = bluetooth.hidVendor().configure(vendorConfig) &&
    bluetooth.hidCustom().configure() &&
    bluetooth.hidCustom().setReportMap(
      CUSTOM_REPORT_MAP, sizeof(CUSTOM_REPORT_MAP)) &&
    bluetooth.hidCustom().addInputReport(CUSTOM_REPORT_ID, CUSTOM_INPUT_SIZE) &&
    bluetooth.hidCustom().addOutputReport(CUSTOM_REPORT_ID, CUSTOM_OUTPUT_SIZE) &&
    bluetooth.hidCustom().addFeatureReport(CUSTOM_REPORT_ID, CUSTOM_FEATURE_SIZE);
  if (!configured)
  {
    Serial.printf("CONFIGURE_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }
  vendorReportSize = vendorConfig.reportSize;

  // The report ID the vendor profile occupies is not available to hidCustom():
  // one Report Map cannot declare two reports under one ID.
  conflictRefused =
    !bluetooth.hidCustom().addInputReport(ESP_BLE_HID_REPORT_ID_VENDOR, 4);
  conflictError = bluetooth.lastErrorName();

  bluetooth.onMtuChanged([](const EspBleMtuChanged &event) {
    negotiatedMtu = event.connection.mtu;
  });
  bluetooth.hidVendor().onOutputReport([](const EspBleHidVendorReport &report) {
    printReport("VENDOR_OUTPUT", report);
  });
  bluetooth.hidVendor().onFeatureReport([](const EspBleHidVendorReport &report) {
    printReport("VENDOR_FEATURE", report);
  });
  bluetooth.hidCustom().onOutputReport([](const EspBleHidVendorReport &report) {
    printReport("CUSTOM_OUTPUT", report);
  });
  bluetooth.hidCustom().onFeatureReport([](const EspBleHidVendorReport &report) {
    printReport("CUSTOM_FEATURE", report);
  });

  EspBleConfig config;
  config.deviceName = DEVICE_NAME;
  if (!bluetooth.begin(config))
  {
    Serial.printf("BEGIN_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }
  if (!bluetooth.advertising().start())
  {
    Serial.printf("ADVERTISE_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  started = true;
  Serial.println("HID_VENDOR_CUSTOM_READY");
}

void loop()
{
  bluetooth.update();
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.printf(
        "READY_STATE started=%u configured=%u size=%u mtu=%u vendor=%u custom=%u\n",
        started ? 1 : 0, configured ? 1 : 0, vendorReportSize, negotiatedMtu,
        bluetooth.hidVendor().ready() ? 1 : 0,
        bluetooth.hidCustom().ready(CUSTOM_REPORT_ID) ? 1 : 0);
    }
    else if (command == 'k')
    {
      Serial.printf("CUSTOM_CONFLICT refused=%u error=%s\n",
        conflictRefused ? 1 : 0, conflictError);
    }
    else if (command == 'v')
    {
      // The whole report, every byte distinct, so a truncated notification is
      // visible as such rather than as a plausible short one.
      uint8_t report[64];
      for (uint8_t index = 0; index < vendorReportSize; ++index)
      {
        report[index] = static_cast<uint8_t>(index + 1);
      }
      Serial.printf("SEND vendor=%u error=%s\n",
        bluetooth.hidVendor().sendInput(report, vendorReportSize) ? 1 : 0,
        bluetooth.lastErrorName());
    }
    else if (command == 'c')
    {
      const uint8_t report[CUSTOM_INPUT_SIZE] = {0x11, 0x22, 0x33, 0x44};
      Serial.printf("SEND custom=%u error=%s\n",
        bluetooth.hidCustom().sendInput(
          CUSTOM_REPORT_ID, report, sizeof(report)) ? 1 : 0,
        bluetooth.lastErrorName());
    }
    else if (command == 'e')
    {
      // The descriptor declares one fixed size, so a shorter report is a
      // mismatch rather than a partial one.
      const uint8_t report[4] = {};
      Serial.printf("SEND short_vendor=%u error=%s\n",
        bluetooth.hidVendor().sendInput(report, sizeof(report)) ? 1 : 0,
        bluetooth.lastErrorName());
    }
    else if (command == 'E')
    {
      const uint8_t report[CUSTOM_INPUT_SIZE] = {};
      Serial.printf("SEND unknown_custom=%u error=%s\n",
        bluetooth.hidCustom().sendInput(9, report, sizeof(report)) ? 1 : 0,
        bluetooth.lastErrorName());
      Serial.printf("READY_UNKNOWN custom9=%u\n",
        bluetooth.hidCustom().ready(9) ? 1 : 0);
    }
  }
  delay(1);
}
