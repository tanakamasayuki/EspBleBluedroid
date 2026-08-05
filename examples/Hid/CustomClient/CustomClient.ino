// en: CustomClient - read a Custom HID device's arbitrary Report Descriptor and
//     drive its reports using the generic GATT client. Pairs with the
//     Hid/CustomDevice example. A HID device exposes several Report
//     characteristics that share UUID 0x2A4D, so every attribute here is named by
//     its distinct attribute HANDLE. Each report's role comes from its Report
//     Reference descriptor (0x2908, report ID + type), which is read BY HANDLE:
//     every Report Reference is 0x2908 under a 0x2A4D characteristic, so a
//     service/characteristic/descriptor UUID triple cannot pick one out.
//     This backend runs ONE central GATT operation per link, so the reads are
//     chained: each one is issued from the previous one's result.
// ja: CustomClient - 汎用GATT clientでCustom HIDデバイスの任意Report Descriptorを読み、
//     Reportを駆動する。Hid/CustomDevice とペア。HIDデバイスは同一UUID 0x2A4Dの
//     Report characteristicを複数持つため、対象はすべて個別のattribute handleで指定する。
//     各Reportの役割はReport Reference descriptor（0x2908、report ID＋type）から読み、
//     その指定もhandleで行う。Report Referenceはどれも「0x2A4Dの下の0x2908」なので、
//     Service/Characteristic/Descriptor UUIDの組では選び分けられない。
//     この後端はlinkあたり同時に1つのCentral GATT操作しか実行しないため、Readは
//     前のResultから次を出す形で数珠つなぎにする。
#include <EspBleBluedroid.h>

static constexpr const char *HID_SERVICE_UUID = "1812";
static constexpr const char *REPORT_UUID = "2a4d";
static constexpr const char *REPORT_REFERENCE_UUID = "2908";

// Report Reference type byte (HID over GATT).
static constexpr uint8_t ReportTypeInput = 1;
static constexpr uint8_t ReportTypeOutput = 2;

// en: The GATT server limit for descriptors in one discovery snapshot is larger, but a
//     Custom HID device declares a handful of reports.
// ja: Discovery snapshotのdescriptor上限はもっと大きいが、Custom HIDデバイスのReportは数個。
static constexpr size_t MaxReports = 8;

EspBleBluedroid bluetooth;
EspBleConnectionId connectionId = 0;
bool connectionRequested = false;
uint16_t inputHandle = 0;
uint16_t outputHandle = 0;
// en: The Report Reference descriptors still to read, and how far along we are.
// ja: これから読むReport Reference descriptorと、その進行位置。
uint16_t referenceHandles[MaxReports];
size_t referenceCount = 0;
size_t referenceIndex = 0;

// Discovered UUIDs are in full 128-bit form (0000XXXX-...); match either way.
static bool uuidIs(const String &uuid, const char *shortUuid)
{
  String lower = uuid;
  lower.toLowerCase();
  String needle = shortUuid;
  needle.toLowerCase();
  return lower == needle || lower.indexOf(needle) == 4;
}

// en: Issue the next Report Reference read, or subscribe once they are all read. One
//     operation per link means the sketch owns this sequencing; a queue would hide it.
// ja: 次のReport Reference Readを出す。すべて読み終えたら購読へ進む。1link 1操作なので
//     この順序制御はsketch側の仕事になる（queueがあれば隠れる部分）。
void readNextReference()
{
  while (referenceIndex < referenceCount)
  {
    if (bluetooth.readDescriptor(connectionId, referenceHandles[referenceIndex]))
    {
      return;
    }
    // en: Could not even start: skip this one rather than stalling the sequence.
    // ja: 開始すらできなかった場合は、列を止めずにこの1件を飛ばす。
    Serial.printf("Report Reference read failed to start: %s\n",
      bluetooth.lastErrorName());
    ++referenceIndex;
  }
  if (inputHandle != 0)
  {
    // en: Subscribing is a GATT operation too, so it waits until the reads are done.
    // ja: 購読もGATT操作なので、Readが終わってから出す。
    bluetooth.subscribe(connectionId, inputHandle, true); // subscribe by handle
  }
}

void setup()
{
  Serial.begin(115200);
  if (!bluetooth.begin())
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }
  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    bluetooth.discoverServices(connection.id);
  });
  bluetooth.onDisconnected([](const EspBleConnection &) {
    connectionId = 0;
    connectionRequested = false;
    inputHandle = outputHandle = 0;
    referenceCount = referenceIndex = 0;
  });
  bluetooth.onServicesDiscovered([](const EspBleGattResult &result) {
    if (!result.success) return;
    // Ask each Report characteristic what it is, by reading its own Report
    // Reference. A descriptor belongs to one characteristic, and the link is the
    // owning value handle: discoveredDescriptor() reports it as
    // characteristicHandle. The UUID pair cannot do it here, because every
    // characteristic is 0x2A4D and every descriptor is 0x2908.
    const size_t characteristicCount =
      bluetooth.discoveredCharacteristicCount(result.connectionId, HID_SERVICE_UUID);
    const size_t descriptorCount =
      bluetooth.discoveredDescriptorCount(result.connectionId, HID_SERVICE_UUID);
    referenceCount = referenceIndex = 0;
    for (size_t index = 0; index < characteristicCount; ++index)
    {
      EspBleGattCharacteristicInfo info;
      if (!bluetooth.discoveredCharacteristic(
            result.connectionId, index, info, HID_SERVICE_UUID))
        continue;
      if (!uuidIs(info.characteristicUuid, REPORT_UUID)) continue;
      for (size_t d = 0; d < descriptorCount; ++d)
      {
        EspBleGattDescriptorInfo descriptor;
        if (!bluetooth.discoveredDescriptor(
              result.connectionId, d, descriptor, HID_SERVICE_UUID))
          continue;
        if (descriptor.characteristicHandle != info.handle) continue;
        if (!uuidIs(descriptor.descriptorUuid, REPORT_REFERENCE_UUID)) continue;
        // en: Collect the handles; they are read one at a time, not all at once.
        // ja: handleを集めるだけ。Readは一括ではなく1つずつ出す。
        if (referenceCount < MaxReports) referenceHandles[referenceCount++] = descriptor.handle;
        break;
      }
    }
    Serial.printf("Reading %u Report Reference descriptors\n",
      static_cast<unsigned>(referenceCount));
    readNextReference();
  });
  bluetooth.onDescriptorRead([](const EspBleGattResult &result) {
    // result.handle is the characteristic that owns the descriptor, so the role
    // read out of the descriptor lands on the right Report characteristic.
    if (result.success && result.value.length() >= 2)
    {
      const uint8_t reportType = static_cast<uint8_t>(result.value[1]);
      if (reportType == ReportTypeInput)
      {
        inputHandle = result.handle;
        Serial.printf("Input report: id=%u handle=%u\n",
          static_cast<uint8_t>(result.value[0]), inputHandle);
      }
      else if (reportType == ReportTypeOutput)
      {
        outputHandle = result.handle;
        Serial.printf("Output report: id=%u handle=%u\n",
          static_cast<uint8_t>(result.value[0]), outputHandle);
      }
    }
    // en: Advance whether it worked or not: a failed read must not stall the rest.
    //     Issuing the next operation from here is safe — callbacks are dispatched
    //     from update(), not from inside the backend's own callback.
    // ja: 成功・失敗にかかわらず進める。1件の失敗で残りを止めない。ここから次の操作を
    //     出して問題ないのは、callbackが後端のcallback内ではなく update() から
    //     配送されるためである。
    ++referenceIndex;
    readNextReference();
  });
  bluetooth.onNotification([](const EspBleGattNotification &notification) {
    if (notification.handle != inputHandle || notification.value.length() < 2) return;
    Serial.printf("Input report: dial delta=%d buttons=%u\n",
      static_cast<int8_t>(notification.value[0]),
      static_cast<uint8_t>(notification.value[1]));
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionRequested || !result.advertisesService(HID_SERVICE_UUID)) return;
    bluetooth.scanner().stop();
    connectionRequested = bluetooth.connect(result);
  });

  EspBleScanConfig scan;
  scan.active = true;
  bluetooth.scanner().start(scan);
  Serial.println("Scanning for a Custom HID device. Send 'o' to write the output LED report.");
}

void loop()
{
  if (Serial.available() > 0 && Serial.read() == 'o' && outputHandle != 0)
  {
    const uint8_t leds = 0x02; // write the output report by handle
    bluetooth.writeCharacteristic(connectionId, outputHandle, &leds, sizeof(leds), true);
  }
  bluetooth.update();
  delay(1);
}
