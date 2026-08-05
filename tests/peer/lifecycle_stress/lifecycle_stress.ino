// What repetition costs. Every other peer suite brings the BLE stack up once and
// leaves it up; this one takes the whole lifecycle round eight times and reports free
// heap, minimum free heap and the FreeRTOS task count after each round.
//
// A leak of a few hundred bytes per connection is invisible in a suite that connects
// once and fatal in a sketch that reconnects all day, and the same goes for a task the
// connect worker forgot to delete or an event queue that never drains. So the numbers
// are compared as *drift* between the second round and the last: the first round pays
// for one-time allocations (controller buffers, NVS handles) and can never be the
// baseline.
//
// Four depths are available, because "something leaks" and "this stage leaks" are
// different findings and a deeper round is the sum of the shallower ones:
//
//   b  begin() / end()
//   n  begin() / scan / end()
//   c  begin() / connect / disconnect / end()
//   g  the whole round, discovery and GATT traffic included
//
// That split is what attributed the leak this suite found (tests/TEST_PLAN.md, "Known
// defects"): begin/end and scan are clean, and the loss appears with the connection.
// Three wrapper-level controls — BLEDevice::init()/deinit(), a reused BLEClient
// connecting and disconnecting, and the wrapper doing exactly this round's shape
// including the MTU request — all lost nothing, so the bytes are this library's and not
// Arduino-ESP32's. Those controls are not kept here: a suite in this repository should
// not depend on the wrapper's own API, and what they showed is recorded in the plan.
//
// The instrument is a raw Arduino-ESP32 peripheral that stays up for the whole run. It
// re-advertises after every disconnect and notifies whatever is written to it, so each
// round needs no serial round trip and the peer's own state cannot drift.

#include <EspBleBluedroid.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *SERVICE_UUID =
  "00120000-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *CHARACTERISTIC_UUID =
  "00120001-b1dd-4d00-9e5a-627564726f69";
static constexpr const char *TARGET_NAME = "Bluedroid Lifecycle Peer";

static constexpr uint8_t TotalCycles = 8;
// A round that stalls is reported and abandoned rather than left to the test's timeout,
// so the log names the stage it stopped at.
static constexpr uint32_t CycleTimeoutMilliseconds = 20000;

// How far a round goes.
enum class Mode : uint8_t
{
  BeginEnd,
  Scan,
  Connect,
  Full,
};

EspBleBluedroid bluetooth;
TaskHandle_t loopTask = nullptr;

Mode mode = Mode::Full;
uint8_t cycle = 0;
bool running = false;
bool finishRequested = false;
bool cycleFailed = false;
const char *stage = "idle";
uint32_t cycleStartedAt = 0;
EspBleConnectionId connectionId = 0;
// Set when the round asks for the disconnect, which is earlier than finishRequested:
// the disconnect event arrives an update() later, and callbacks still queued behind the
// notification (the write completion) are dispatched in between.
bool closing = false;
int disconnectReason = 0;
// Heap sampled where memory is taken and where it is supposed to come back, reported on
// the round's own line. Which stage loses it is a different question from how much.
uint32_t heapRoundStart = 0;
uint32_t heapAfterBegin = 0;
uint32_t heapConnected = 0;
uint32_t heapDisconnected = 0;
uint32_t heapBeforeEnd = 0;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

const char *modeName()
{
  switch (mode)
  {
    case Mode::BeginEnd: return "beginend";
    case Mode::Scan: return "scan";
    case Mode::Connect: return "connect";
    default: return "full";
  }
}

void requestFinish(bool failed)
{
  cycleFailed = cycleFailed || failed;
  finishRequested = true;
}

// Where the round got to, for the report and for a stalled round's diagnosis. It stops
// at the disconnect, not at the disconnect *event*: the write completion is dispatched
// after the notification that asked for the disconnect but before the disconnect
// arrives, so a trace guarded on the event alone gets overwritten and says nothing
// about where the round actually stopped.
void setStage(const char *name)
{
  if (!closing && !finishRequested) stage = name;
}

void startCycle()
{
  ++cycle;
  heapRoundStart = ESP.getFreeHeap();
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
  heapAfterBegin = ESP.getFreeHeap();

  // Registered again after every begin(): a library that kept the previous cycle's
  // callbacks alive would be leaking them, and one that dropped them would stall here
  // instead.
  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    heapConnected = ESP.getFreeHeap();
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
    heapDisconnected = ESP.getFreeHeap();
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
    // The peer notifies whatever is written to it, so this write is what produces the
    // notification below — no serial round trip inside a cycle.
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
    // Nothing was connected; keep the reported shape so one pattern reads every mode.
    disconnectReason = 22;
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

void finishCycle()
{
  finishRequested = false;
  const size_t dropped = bluetooth.droppedEventCount();
  heapBeforeEnd = ESP.getFreeHeap();
  bluetooth.end();
  // Read after end(), so whatever the stack held is already returned. end() returns
  // void, so what is reported is the state it left behind: initialized() has to be
  // false, or the next begin() is not a fresh one.
  Serial.printf(
    "CYCLE %u ok=%u ended=%u stage=%s reason=%d heap=%u minheap=%u tasks=%u "
    "dropped=%u context=%s mode=%s start=%u begun=%u conn=%u disc=%u pre=%u\n",
    cycle, cycleFailed ? 0 : 1, bluetooth.initialized() ? 0 : 1, stage,
    disconnectReason,
    static_cast<unsigned>(ESP.getFreeHeap()),
    static_cast<unsigned>(ESP.getMinFreeHeap()),
    static_cast<unsigned>(uxTaskGetNumberOfTasks()),
    static_cast<unsigned>(dropped), contextName(), modeName(),
    static_cast<unsigned>(heapRoundStart), static_cast<unsigned>(heapAfterBegin),
    static_cast<unsigned>(heapConnected), static_cast<unsigned>(heapDisconnected),
    static_cast<unsigned>(heapBeforeEnd));
  if (cycle >= TotalCycles)
  {
    running = false;
    Serial.printf("CYCLES_DONE count=%u mode=%s\n", cycle, modeName());
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
      // end() runs here rather than inside the callback that asked for it: tearing the
      // stack down from inside its own dispatch is not something a sketch should have
      // to reason about.
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
              command == 'c') && !running)
    {
      mode = command == 'b' ? Mode::BeginEnd
           : command == 'n' ? Mode::Scan
           : command == 'c' ? Mode::Connect
                            : Mode::Full;
      running = true;
      cycle = 0;
      startCycle();
    }
  }
  delay(1);
}
