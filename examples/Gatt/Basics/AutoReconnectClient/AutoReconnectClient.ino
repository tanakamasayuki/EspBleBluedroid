// en: AutoReconnectClient - connect to the Gatt/Basics/NotifyServer example,
//     subscribe, and keep the link alive by hand. EspBleBluedroid has no
//     setAutoReconnect() and no persistent subscriptions, so this sketch shows
//     the smallest correct replacement: remember the peer address, reconnect
//     after a back-off when the link drops, and subscribe again on every link.
// ja: AutoReconnectClient - Gatt/Basics/NotifyServer example へ接続して購読し、
//     linkの維持を自分で行う。EspBleBluedroidには setAutoReconnect() も購読の
//     自動復元もないため、その最小の代替を示す。peerのaddressを覚えておき、切断
//     したらback-offを置いて再接続し、linkごとに購読をやり直す。
#include <EspBleBluedroid.h>

static constexpr const char *SERVICE_UUID = "71756360-5fa4-43bc-9003-6e6f74696679";
static constexpr const char *CHARACTERISTIC_UUID = "71756361-5fa4-43bc-9003-6e6f74696679";

// en: Wait before reconnecting. Retrying immediately just burns radio time while
//     the peer is still restarting its advertising.
// ja: 再接続前に待つ時間。相手がAdvertisingを再開する前に即座に再試行しても、
//     電波の時間を無駄にするだけ。
static constexpr uint32_t RECONNECT_DELAY_MS = 1000;

EspBleBluedroid bluetooth;

// en: The remembered peer. This is what setAutoReconnect() would keep for us.
// ja: 記憶しておくpeer。setAutoReconnect() が内部で保持してくれる情報にあたる。
String peerAddress;
EspBleAddressType peerAddressType = EspBleAddressType::Public;
bool peerKnown = false;

bool connectionRequested = false;
uint32_t reconnectAtMs = 0;

void setup()
{
  Serial.begin(115200);

  EspBleConfig config;
  config.deviceName = "Bluedroid AutoReconnect Client";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    Serial.printf("Connected to %s\n", connection.peerAddress.c_str());
    // en: Remember the peer so the next attempt needs no scan.
    // ja: 次回はscanなしで済むようにpeerを覚えておく。
    peerAddress = connection.peerAddress;
    peerAddressType = connection.peerAddressType;
    peerKnown = true;
    // en: Every link starts unsubscribed: a CCCD lives in the connection, and
    //     nothing on either side restores it automatically.
    // ja: linkごとに購読はゼロから。CCCDは接続に属し、どちら側も自動では復元しない。
    bluetooth.subscribe(connection.id, SERVICE_UUID, CHARACTERISTIC_UUID, true);
  });
  bluetooth.onSubscribed([](const EspBleGattResult &result) {
    Serial.printf("Subscription %s\n",
      result.success ? "active" : result.detail.c_str());
  });
  bluetooth.onDisconnected([](const EspBleConnection &) {
    // en: Schedule the reconnect instead of calling connect() here. This callback
    //     is delivered from update(), and issuing the next attempt from loop()
    //     keeps the back-off in one place.
    // ja: ここで connect() を呼ばずに再接続を予約する。このcallbackは update() から
    //     配送されるため、次の試行を loop() 側へ寄せてback-offを1か所にまとめる。
    connectionRequested = false;
    reconnectAtMs = millis() + RECONNECT_DELAY_MS;
    Serial.println("Disconnected - reconnecting shortly.");
  });
  bluetooth.onConnectionFailed([](const EspBleConnectionFailure &failure) {
    // en: A peer that is not advertising yet fails with a timeout. Keep retrying.
    // ja: まだAdvertisingしていない相手はtimeoutで失敗する。再試行を続ける。
    Serial.printf("Connect failed: %s - retrying\n", failure.detail.c_str());
    connectionRequested = false;
    reconnectAtMs = millis() + RECONNECT_DELAY_MS;
  });
  bluetooth.onNotification([](const EspBleGattNotification &notification) {
    Serial.printf("Notification: %s\n", notification.value.c_str());
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &scanResult) {
    if (connectionRequested || !scanResult.advertisesService(SERVICE_UUID))
    {
      return;
    }
    bluetooth.scanner().stop();
    connectionRequested = bluetooth.connect(scanResult);
  });

  // en: Only the first connection needs a scan. Afterwards the remembered
  //     address is used directly, which is what makes reconnecting fast.
  // ja: scanが必要なのは最初の接続だけ。以降は記憶したaddressを直接使うので
  //     再接続が速い。
  EspBleScanConfig scanConfig;
  scanConfig.active = true;
  bluetooth.scanner().start(scanConfig);
}

void loop()
{
  // en: The reconnect is driven from here, so nothing runs inside a callback.
  // ja: 再接続はここから駆動する。callbackの中では何も起動しない。
  if (peerKnown && !connectionRequested && reconnectAtMs != 0 &&
      static_cast<int32_t>(millis() - reconnectAtMs) >= 0)
  {
    reconnectAtMs = 0;
    connectionRequested =
      bluetooth.connect(peerAddress.c_str(), peerAddressType);
    if (!connectionRequested)
    {
      Serial.printf("Reconnect request rejected: %s\n",
        bluetooth.lastErrorDetail().c_str());
      reconnectAtMs = millis() + RECONNECT_DELAY_MS;
    }
  }

  // en: Connection, subscription, and notification events are all delivered here.
  // ja: 接続・購読・Notificationのイベントはすべてここから配送される。
  bluetooth.update();
  delay(1);
}
