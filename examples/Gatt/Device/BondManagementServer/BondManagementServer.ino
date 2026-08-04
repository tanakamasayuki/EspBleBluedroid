// en: BondManagementServer - standard Bond Management Service (0x181E). Bond
//     Management Feature (0x2AA5) is a readable uint24 bit field of supported
//     operations; the Bond Management Control Point (0x2AA4) is writable and
//     receives op codes in onWritten. On "Delete bond of requesting device"
//     (0x03), the server removes that peer's bond after it disconnects.
// ja: BondManagementServer - 標準Bond Management Service（0x181E）。Bond
//     Management Feature（0x2AA5）は対応操作のread可能なuint24 bit field、Bond
//     Management Control Point（0x2AA4）はwritableでop codeをonWrittenで受け取る。
//     EspBleBluedroidはPeripheral connection snapshotを公開しないため、どのpeerが
//     op codeを書いたのかをServerは知れない。そのため「Delete bond of requesting
//     device（LE）」（0x03）では、少し後にLEのbondをすべて削除する。Feature bit field
//     にもそのように申告する。READMEを参照。
#include <EspBleBluedroid.h>

static constexpr const char *BMS_SERVICE_UUID = "181e";
static constexpr const char *BOND_MANAGEMENT_CONTROL_POINT_UUID = "2aa4";
static constexpr const char *BOND_MANAGEMENT_FEATURE_UUID = "2aa5";

EspBleBluedroid bluetooth;
EspBleGattService bmsServiceService;
EspBleGattCharacteristic bondManagementControlPointCharacteristic;
EspBleGattCharacteristic bondManagementFeatureCharacteristic;
// en: uint24 Feature bit field. Bit 10 = "Delete all bonds on server (LE)"
//     supported, which is what this server can actually carry out. The
//     per-requesting-device bits stay clear because the peer address is unknown
//     on this side.
// ja: uint24のFeature bit field。bit 10 = 「Delete all bonds on server（LE）」に
//     対応、という申告で、このServerが実際に実行できるのはこれ。要求元単位の
//     bitは、この側でpeer addressが分からないため立てない。
const uint8_t feature[3] = {0x00, 0x04, 0x00};

// en: When to run the deferred delete (0 = nothing scheduled). The delete is
//     deferred so the client's own disconnect happens first.
// ja: 遅延削除を実行する時刻（0は予約なし）。Client側の切断を先に済ませるために遅らせる。
uint32_t deleteBondsAtMs = 0;
static constexpr uint32_t DELETE_DELAY_MS = 3000;

static void deleteAllLeBonds()
{
  const size_t before = bluetooth.bondCount();
  if (bluetooth.deleteAllBonds())
  {
    Serial.printf("Deleted %u bond(s); remaining=%u\n",
      static_cast<unsigned>(before),
      static_cast<unsigned>(bluetooth.bondCount()));
  }
  else
  {
    Serial.printf("Bond deletion failed: %s\n", bluetooth.lastErrorDetail().c_str());
  }
}

void setup()
{
  Serial.begin(115200);

  EspBleGattCharacteristicConfig controlConfig;
  controlConfig.writable = true;
  EspBleGattCharacteristicConfig featureConfig;
  featureConfig.readable = true;
  auto &server = bluetooth.gattServer();
  bmsServiceService = server.addService(BMS_SERVICE_UUID);
  bondManagementControlPointCharacteristic = server.addCharacteristic(bmsServiceService, BOND_MANAGEMENT_CONTROL_POINT_UUID, controlConfig);
  bondManagementFeatureCharacteristic = server.addCharacteristic(bmsServiceService, BOND_MANAGEMENT_FEATURE_UUID, featureConfig);
  server.setValue(bondManagementFeatureCharacteristic, feature, sizeof(feature));

  server.onWritten([](const EspBleGattWrite &write) {
    if (!write.characteristicUuid.equalsIgnoreCase(BOND_MANAGEMENT_CONTROL_POINT_UUID) || write.value.length() < 1)
      return;
    const uint8_t opCode = static_cast<uint8_t>(write.value[0]);
    Serial.printf("Bond Management op code: %u\n", opCode);
    // en: 0x03 = Delete bond of requesting device (LE), 0x06 = Delete all bonds
    //     on server (LE). Both are answered the same way here, because without a
    //     peripheral connection snapshot the requesting peer cannot be
    //     identified. The delete is deferred so the client disconnects first.
    // ja: 0x03 = Delete bond of requesting device（LE）、0x06 = Delete all bonds on
    //     server（LE）。ここではどちらも同じ扱いになる。Peripheral connection
    //     snapshotが無く要求元を特定できないため。削除はClientの切断を待って遅延実行する。
    if (opCode == 0x03 || opCode == 0x06)
    {
      deleteBondsAtMs = millis() + DELETE_DELAY_MS;
      Serial.printf("Deleting LE bonds in %u ms\n",
        static_cast<unsigned>(DELETE_DELAY_MS));
    }
  });

  EspBleConfig config;
  config.deviceName = "Bluedroid Bond Management";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }
  bluetooth.advertising().addServiceUuid(BMS_SERVICE_UUID);
  bluetooth.advertising().start();
}

void loop()
{
  // en: Run the deferred delete from loop(), never from the write callback: the
  //     bond store update is synchronous and waits for Bluedroid's persistent
  //     store, so it must not run inside event delivery.
  // ja: 遅延削除はWriteのcallbackではなくloop()から実行する。bond storeの更新は
  //     同期的でBluedroidの永続storeを待つため、イベント配送の中では行わない。
  if (deleteBondsAtMs != 0 &&
      static_cast<int32_t>(millis() - deleteBondsAtMs) >= 0)
  {
    deleteBondsAtMs = 0;
    deleteAllLeBonds();
  }

  bluetooth.update();
  delay(1);
}
