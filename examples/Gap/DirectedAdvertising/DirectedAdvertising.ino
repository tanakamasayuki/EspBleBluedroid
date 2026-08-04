// en: DirectedAdvertising - advertise to exactly one peer. A directed advertisement
//     names the target address in the PDU, so only that peer may connect, and it
//     carries no payload at all.
// ja: DirectedAdvertising - 相手を1台に限定してadvertiseする。有向advertisingはPDUに
//     宛先アドレスを載せるため、その相手だけが接続でき、payloadは一切載らない。
#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID = "3d9b1c40-6f2e-4a8b-9f31-646972656374";

// en: Address of the central to advertise to. That board can print its own with
//     bluetooth.localAddress(); EspBleAddressType must match its localAddressType().
// ja: advertise先Centralのアドレス。相手のボードでは bluetooth.localAddress() で表示できる。
//     EspBleAddressType は相手の localAddressType() に合わせる。
static constexpr const char *TARGET_CENTRAL = "aa:bb:cc:dd:ee:ff";
static constexpr EspBleAddressType TARGET_TYPE = EspBleAddressType::Public;

EspBleBluedroid bluetooth;

static void advertiseUndirected()
{
  auto &advertising = bluetooth.advertising();
  // en: Clearing the target restores the normal payload; it was kept while
  //     directed, just not transmitted.
  // ja: targetを解除すると通常のpayloadに戻る。有向中も保持されていて、
  //     送信されていなかっただけ。
  advertising.clearDirectedTarget();
  advertising.start();
  Serial.println("Undirected: anyone may connect.");
}

static void advertiseDirected()
{
  auto &advertising = bluetooth.advertising();
  advertising.stop();
  // en: The third argument selects High Duty Cycle: 3.75 ms interval for at most
  //     1.28 s, for the fastest possible reconnection to a known peer. It stops
  //     by itself when that runs out. false (the default) keeps the configured
  //     interval and advertises until stop().
  // ja: 第3引数でHigh Duty Cycleを選ぶ。3.75 ms間隔で最大1.28秒送出し、既知の相手へ
  //     最速で再接続する。時間切れで自動的に止まる。false（既定）なら設定した間隔で
  //     stop() まで続く。
  if (!advertising.setDirectedTarget(TARGET_CENTRAL, TARGET_TYPE, false))
  {
    Serial.printf("Directed target failed: %s (%s)\n", bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }
  if (!advertising.start())
  {
    Serial.printf("Advertising failed: %s (%s)\n", bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }
  Serial.printf("Directed at %s. No payload is sent.\n", TARGET_CENTRAL);
}

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "Bluedroid Directed";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE init failed: %s (%s)\n", bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }

  // en: No peripheral-side connect/disconnect callback exists here (README):
  //     onConnected() covers links this device opens with connect(). Watch the
  //     target central for the result, and restart advertising with h or l.
  // ja: Peripheral側の接続・切断callbackはない（README参照）。onConnected() はこの機器が
  //     connect() で開いたlinkを表す。結果は宛先Central側で確認し、Advertisingの再開は
  //     h または l で行う。

  auto &advertising = bluetooth.advertising();
  advertising.setName("Bluedroid Directed");
  advertising.addServiceUuid(SERVICE_UUID);

  // en: Start undirected so the central can find this device once and learn its
  //     address. A directed advertisement cannot be matched by service UUID
  //     because it has no payload to match against.
  // ja: まず無向で始め、Centralに一度見つけてもらってアドレスを学習させる。
  //     有向advertisingは照合するpayloadを持たないため、Service UUIDでは拾えない。
  if (!advertising.start())
  {
    Serial.printf("Advertising failed: %s (%s)\n", bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }
  Serial.printf("Advertising as %s. Send 'd' to direct it at %s.\n",
    bluetooth.localAddress().c_str(), TARGET_CENTRAL);
}

void loop()
{
  // en: 'd' switches to directed advertising, 'u' back to undirected.
  // ja: 'd' で有向advertisingへ切り替え、'u' で無向へ戻す。
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'd')
    {
      advertiseDirected();
    }
    else if (command == 'u')
    {
      bluetooth.advertising().stop();
      advertiseUndirected();
    }
  }

  bluetooth.update();
  delay(1);
}
