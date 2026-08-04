// en: AcceptList - one Filter Accept List, two uses. It restricts who may connect
//     to this device (BLE has no "approve this connection request" callback: the
//     controller decides before the application hears about it), and it also
//     filters which advertisers a scan reports.
// ja: AcceptList - 1つのFilter Accept Listを2通りに使う。この機器へ接続できる相手を
//     制限し（BLEには「接続要求を承認する」callbackが無く、アプリに届く前に
//     controllerが判断する）、同じリストでscanに報告される相手も絞り込む。
#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID = "5266f727-49d7-4eaf-a6f1-6163636570";

// en: Replace with the address of the peer you want to allow. That board can
//     print its own address with bluetooth.localAddress().
// ja: 許可したい相手のアドレスに置き換える。相手のボードでは
//     bluetooth.localAddress() で自分のアドレスを表示できる。
static constexpr const char *ALLOWED_PEER = "aa:bb:cc:dd:ee:ff";

EspBleBluedroid bluetooth;

// en: Whether the running scan is filtered by the accept list.
// ja: 実行中のscanがaccept listで絞り込まれているか。
static bool scanFiltered = false;

static void startScan(bool filtered)
{
  bluetooth.scanner().stop();

  EspBleScanConfig scan;
  // en: With acceptListOnly the controller drops advertisements from anyone not
  //     on the list, so they never reach onResult at all. Cheaper and more
  //     reliable than comparing addresses in the callback.
  // ja: acceptListOnlyを立てると、リストに無い相手のadvertisementはcontrollerが捨て、
  //     onResultへ届かない。callbackでアドレスを比較するより安く、確実。
  scan.acceptListOnly = filtered;
  scan.durationSeconds = 5;

  if (!bluetooth.scanner().start(scan))
  {
    Serial.printf("Scan failed: %s (%s)\n", bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }
  scanFiltered = filtered;
  Serial.printf("Scanning for 5 s (%s)\n", filtered ? "accept list only" : "everyone");
}

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "Bluedroid Accept List";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE init failed: %s (%s)\n", bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  // en: The accept list lives in the controller and is shared by advertising and
  //     scanning. Entries are matched by address, so a peer that rotates an RPA
  //     can only be listed usefully once bonded (then its identity address is
  //     what matters).
  // ja: accept listはcontroller側にあり、advertisingとscanで共通。照合はアドレス単位
  //     なので、RPAを回転させる相手はbonding後（identity addressが効くようになって
  //     から）でないと登録できない。
  if (!bluetooth.addToAcceptList(ALLOWED_PEER, EspBleAddressType::Public))
  {
    Serial.printf("Accept list failed: %s (%s)\n", bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  // en: There is no peripheral-side connect/disconnect callback here (see the
  //     README): onConnected() describes links this device opens with connect().
  //     Confirm the filtering from the central: a listed peer connects, an
  //     unlisted one sees a connection failure or timeout.
  // ja: Peripheral側の接続・切断callbackはない（README参照）。onConnected() はこの機器が
  //     connect() で開いたlinkを表す。フィルタの効果はCentral側で確認する。リストに
  //     載る相手は接続でき、載らない相手は接続失敗またはtimeoutになる。

  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    Serial.printf(
      "Advertiser %s rssi=%d (%s scan)\n",
      result.address.c_str(),
      result.rssi,
      scanFiltered ? "filtered" : "open");
  });

  auto &advertising = bluetooth.advertising();
  advertising.setName("Bluedroid Accept List");
  advertising.addServiceUuid(SERVICE_UUID);
  // en: ConnectionFromAcceptList still lets anyone scan and see this device; it
  //     only rejects connection requests. Use Both to also hide from scan
  //     requests, or ScanRequestFromAcceptList to filter only those.
  // ja: ConnectionFromAcceptListはscan自体は誰にでも許し、接続要求だけを拒否する。
  //     scan requestも制限するならBoth、scan requestだけならScanRequestFromAcceptList。
  advertising.setFilterPolicy(EspBleAdvertisingFilterPolicy::ConnectionFromAcceptList);

  if (!advertising.start())
  {
    Serial.printf("Advertising failed: %s (%s)\n", bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }
  Serial.printf("Advertising. Only %s may connect.\n", ALLOWED_PEER);
  Serial.println("Commands: 'o' open policy, 'r' restrict, 'f' filtered scan, 'a' scan everyone");
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    // en: 'o' opens the policy so any central may connect again, 'r' restricts it.
    // ja: 'o' でpolicyを開放して誰でも接続可能にし、'r' で再び制限する。
    if (command == 'o' || command == 'r')
    {
      auto &advertising = bluetooth.advertising();
      advertising.stop();
      advertising.setFilterPolicy(
        command == 'o' ? EspBleAdvertisingFilterPolicy::Any
                       : EspBleAdvertisingFilterPolicy::ConnectionFromAcceptList);
      advertising.start();
      Serial.printf(
        "Policy: %s (accept list has %u entries)\n",
        command == 'o' ? "open" : "restricted",
        static_cast<unsigned>(bluetooth.acceptListCount()));
    }
    // en: The same list on the scan side: 'f' reports only listed advertisers.
    // ja: 同じリストをscan側で使う。'f' はリストに載る相手だけを報告する。
    else if (command == 'f' || command == 'a')
    {
      startScan(command == 'f');
    }
  }

  bluetooth.update();
  delay(1);
}
