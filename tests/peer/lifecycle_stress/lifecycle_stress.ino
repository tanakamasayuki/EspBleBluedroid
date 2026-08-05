// What repetition costs. Every other peer suite runs the BLE stack up once and
// leaves it up; this one takes the whole lifecycle round eight times —
// begin() → scan → connect → discover → read → subscribe → write → notification →
// disconnect → end() — and reports free heap, minimum free heap and the FreeRTOS
// task count after each round.
//
// A leak of a few hundred bytes per connection is invisible in a suite that
// connects once and fatal in a sketch that reconnects all day, and the same goes for
// a task the connect worker forgot to delete or an event queue that never drains.
// Those are the failures this scenario exists to catch, so what it asserts is not a
// single number but the *drift* between the second round and the last: the first
// round pays for one-time allocations (controller buffers, NVS handles) and can
// never be the baseline.
//
// The instrument is a raw Arduino-ESP32 peripheral that stays up for the whole run.
// It re-advertises after every disconnect and notifies whatever is written to it, so
// each round needs no serial round trip and the peer's own state cannot drift.

#include <EspBleBluedroid.h>
#include <BLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *SERVICE_UUID =
  "00120000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *CHARACTERISTIC_UUID =
  "00120001-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *TARGET_NAME = "Bluedroid Lifecycle Peer";

static constexpr uint8_t TotalCycles = 8;
// How far a round goes. Reporting the same numbers for a shorter round is what turns
// "something leaks" into "this stage leaks": the full round is the sum of these.
enum class Mode : uint8_t
{
  BeginEnd,     // begin() / end() and nothing else
  Scan,         // begin() / scan / end()
  Connect,      // begin() / connect / disconnect / end()
  Full,         // the whole round, including discovery and GATT traffic
  RawWrapper,   // BLEDevice::init() / deinit(), so the wrapper can be told apart
  RawConnect,   // the wrapper's own connect / disconnect, for the same reason
};
Mode mode = Mode::Full;

const char *modeName()
{
  switch (mode)
  {
    case Mode::BeginEnd: return "beginend";
    case Mode::Scan: return "scan";
    case Mode::Connect: return "connect";
    case Mode::RawWrapper: return "raw";
    case Mode::RawConnect: return "rawconnect";
    default: return "full";
  }
}
// A round that stalls is reported and abandoned rather than left to the test's
// timeout, so the log names the stage it stopped at.
static constexpr uint32_t CycleTimeoutMilliseconds = 20000;

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;

uint8_t cycle = 0;
bool running = false;
bool finishRequested = false;
bool cycleFailed = false;
const char *stage = "idle";
uint32_t cycleStartedAt = 0;
EspBleConnectionId connectionId = 0;
// Set when the round asks for the disconnect, which is earlier than finishRequested:
// the disconnect event arrives an update() later, and callbacks still queued behind
// the notification (the write completion) are dispatched in between.
bool closing = false;
int disconnectReason = 0;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void requestFinish(bool failed)
{
  cycleFailed = cycleFailed || failed;
  finishRequested = true;
}

// Where the round got to, for the report and for a stalled round's diagnosis. It
// stops at the disconnect, not at the disconnect *event*: the write completion is
// dispatched after the notification that asked for the disconnect but before the
// disconnect arrives, so a trace guarded on the event alone gets overwritten and
// says nothing about where the round actually stopped.
void setStage(const char *name)
{
  if (!closing && !finishRequested) stage = name;
}

void startCycle()
{
  ++cycle;
  stage = "begin";
  cycleFailed = false;
  connectionId = 0;
  closing = false;
  disconnectReason = 0;
  cycleStartedAt = millis();

  EspBleConfig config;
  config.deviceName = "Bluedroid Lifecycle";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BEGIN_FAILED cycle=%u %s\n", cycle, bluetooth.lastErrorName());
    requestFinish(true);
    return;
  }

  // Registered again after every begin(): a library that kept the previous cycle's
  // callbacks alive would be leaking them, and one that dropped them would stall
  // here instead.
  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    if (mode == Mode::Connect)
    {
      setStage("disconnect");
      closing = true;
      if (!bluetooth.disconnect(connection.id)) requestFinish(true);
      return;
    }
    setStage("discover");
    if (!bluetooth.discoverServices(connection.id))
    {
      requestFinish(true);
    }
  });
  bluetooth.onDisconnected([](const EspBleConnection &connection) {
    connectionId = 0;
    // Reported per round: a round that ended because this sketch asked (HCI 0x16,
    // terminated by the local host) is a different fact from one whose link dropped,
    // and only the first makes the round's heap numbers mean anything.
    disconnectReason = connection.disconnectReason;
    requestFinish(false);
  });
  bluetooth.onServicesDiscovered([](const EspBleGattResult &result) {
    if (!result.success)
    {
      requestFinish(true);
      return;
    }
    setStage("read");
    if (!bluetooth.readCharacteristic(
          result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID))
    {
      requestFinish(true);
    }
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    if (!result.success)
    {
      requestFinish(true);
      return;
    }
    setStage("subscribe");
    if (!bluetooth.subscribe(result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID))
    {
      requestFinish(true);
    }
  });
  bluetooth.onSubscribed([](const EspBleGattResult &result) {
    if (!result.success)
    {
      requestFinish(true);
      return;
    }
    // The peer notifies whatever is written to it, so this write is what produces
    // the notification below — no serial round trip inside a cycle.
    setStage("write");
    const uint8_t value[] = {0x5a};
    if (!bluetooth.writeCharacteristic(
          result.connectionId, SERVICE_UUID, CHARACTERISTIC_UUID, value,
          sizeof(value), true))
    {
      requestFinish(true);
    }
  });
  bluetooth.onCharacteristicWritten([](const EspBleGattResult &result) {
    if (!result.success) requestFinish(true);
    else setStage("notification");
  });
  bluetooth.onNotification([](const EspBleGattNotification &notification) {
    setStage("disconnect");
    closing = true;
    if (!bluetooth.disconnect(notification.connectionId))
    {
      requestFinish(true);
    }
  });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectionId != 0 || !bluetooth.scanner().isScanning()) return;
    if (result.name != String(TARGET_NAME)) return;
    bluetooth.scanner().stop();
    setStage("connect");
    if (!bluetooth.connect(result, 10000)) requestFinish(true);
  });

  if (mode == Mode::BeginEnd)
  {
    setStage("disconnect");
    disconnectReason = 22;  // nothing was connected; keep the reported shape
    requestFinish(false);
    return;
  }

  stage = "scan";
  EspBleScanConfig scanConfig;
  scanConfig.active = true;
  if (!bluetooth.scanner().start(scanConfig))
  {
    Serial.printf("SCAN_FAILED cycle=%u %s\n", cycle, bluetooth.lastErrorName());
    requestFinish(true);
    return;
  }
  if (mode == Mode::Scan)
  {
    bluetooth.scanner().stop();
    setStage("disconnect");
    disconnectReason = 22;
    requestFinish(false);
  }
}

void runRawWrapperCycles()
{
  // The wrapper's own lifecycle, with none of this library involved: if the same
  // bytes go missing here, they are not this library's to give back.
  for (uint8_t index = 1; index <= TotalCycles; ++index)
  {
    BLEDevice::init("Bluedroid Raw");
    // release=false: deinit(true) hands the BT controller's memory back to the heap
    // for good, and nothing can bring BLE up again afterwards — which silently turned
    // the first attempt at this comparison into eight instant failures per mode.
    BLEDevice::deinit(false);
    delay(200);
    Serial.printf(
      "CYCLE %u ok=1 ended=1 stage=disconnect reason=22 heap=%u minheap=%u "
      "tasks=%u dropped=0 context=loop mode=raw\n",
      index, static_cast<unsigned>(ESP.getFreeHeap()),
      static_cast<unsigned>(ESP.getMinFreeHeap()),
      static_cast<unsigned>(uxTaskGetNumberOfTasks()));
  }
  Serial.printf("CYCLES_DONE count=%u\n", TotalCycles);
}

void runRawConnectCycles()
{
  // The wrapper's own connect / disconnect, with none of this library involved and
  // one client reused throughout — the control for the connect-path cost. Without it,
  // "a connection costs 200 bytes" cannot be attributed to either side.
  BLEDevice::init("Bluedroid Raw Connect");
  BLEClient *client = BLEDevice::createClient();
  for (uint8_t index = 1; index <= TotalCycles; ++index)
  {
    BLEScan *scan = BLEDevice::getScan();
    scan->setActiveScan(true);
    BLEScanResults *results = scan->start(3, false);
    BLEAdvertisedDevice *target = nullptr;
    for (int found = 0; results != nullptr && found < results->getCount(); ++found)
    {
      BLEAdvertisedDevice device = results->getDevice(found);
      if (device.haveName() && device.getName() == String(TARGET_NAME))
      {
        target = new BLEAdvertisedDevice(device);
        break;
      }
    }
    bool ok = false;
    if (target != nullptr)
    {
      ok = client->connect(target);
      if (ok) client->disconnect();
      delete target;
    }
    scan->clearResults();
    delay(500);
    Serial.printf(
      "CYCLE %u ok=%u ended=1 stage=disconnect reason=22 heap=%u minheap=%u "
      "tasks=%u dropped=0 context=loop mode=rawconnect\n",
      index, ok ? 1 : 0, static_cast<unsigned>(ESP.getFreeHeap()),
      static_cast<unsigned>(ESP.getMinFreeHeap()),
      static_cast<unsigned>(uxTaskGetNumberOfTasks()));
  }
  BLEDevice::deinit(false);
  Serial.printf("CYCLES_DONE count=%u\n", TotalCycles);
}

void finishCycle()
{
  finishRequested = false;
  const size_t dropped = bluetooth.droppedEventCount();
  bluetooth.end();
  // Read after end(), so whatever the stack held is already returned. end() returns
  // void, so what is reported is the state it left behind: initialized() has to be
  // false, or the next begin() is not a fresh one.
  Serial.printf(
    "CYCLE %u ok=%u ended=%u stage=%s reason=%d heap=%u minheap=%u tasks=%u "
    "dropped=%u context=%s mode=%s\n",
    cycle, cycleFailed ? 0 : 1, bluetooth.initialized() ? 0 : 1, stage,
    disconnectReason,
    static_cast<unsigned>(ESP.getFreeHeap()),
    static_cast<unsigned>(ESP.getMinFreeHeap()),
    static_cast<unsigned>(uxTaskGetNumberOfTasks()),
    static_cast<unsigned>(dropped), contextName(), modeName());
  if (cycle >= TotalCycles)
  {
    running = false;
    Serial.printf("CYCLES_DONE count=%u\n", cycle);
    return;
  }
  startCycle();
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();
  Serial.printf("LIFECYCLE_READY heap=%u tasks=%u\n",
    static_cast<unsigned>(ESP.getFreeHeap()),
    static_cast<unsigned>(uxTaskGetNumberOfTasks()));
}

void loop()
{
  bluetooth.update();
  if (running)
  {
    if (finishRequested)
    {
      // end() runs here rather than inside the callback that asked for it: tearing
      // the stack down from inside its own dispatch is not something a sketch should
      // have to reason about.
      finishCycle();
    }
    else if (static_cast<uint32_t>(millis() - cycleStartedAt) >
             CycleTimeoutMilliseconds)
    {
      requestFinish(true);
    }
  }
  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.printf("STATE running=%u cycle=%u stage=%s heap=%u tasks=%u\n",
        running ? 1 : 0, cycle, stage,
        static_cast<unsigned>(ESP.getFreeHeap()),
        static_cast<unsigned>(uxTaskGetNumberOfTasks()));
    }
    else if ((command == 'g' || command == 'b' || command == 'n' ||
              command == 'c' || command == 'w' || command == 'W') && !running)
    {
      mode = command == 'b' ? Mode::BeginEnd
           : command == 'n' ? Mode::Scan
           : command == 'c' ? Mode::Connect
           : command == 'w' ? Mode::RawWrapper
           : command == 'W' ? Mode::RawConnect
                            : Mode::Full;
      if (mode == Mode::RawWrapper)
      {
        runRawWrapperCycles();
      }
      else if (mode == Mode::RawConnect)
      {
        runRawConnectCycles();
      }
      else
      {
        running = true;
        cycle = 0;
        startCycle();
      }
    }
  }
  delay(1);
}
