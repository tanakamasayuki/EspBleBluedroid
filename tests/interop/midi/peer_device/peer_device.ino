// The Bluedroid half of the cross-stack BLE MIDI scenario: the library under
// test, in whichever role the EspBle side is not.
//
// Both libraries carry the same codec and the same profile helper, so what is
// being compared here is not the byte tables — those are already pinned by
// tests/unit/midi and tests/unit/api_parity — but whether the transport agrees
// across two host stacks: the CCCD write, notifications against a negotiated MTU,
// Write Without Response, and a SysEx that spans packets. Each side encodes and
// decodes through its own library, so agreement is real rather than assumed.
//
// Output is prefixed BLUEDROID_ so a log line never leaves it ambiguous which
// stack produced it, and callback context is reported because dispatch from
// `update()` is part of this library's contract.

#include <EspBleBluedroid.h>
#include <EspBleMidiProfile.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *DEVICE_NAME = "Bluedroid MIDI Device 0106";
static constexpr const char *HOST_NAME = "Bluedroid MIDI Host 0106";
// The counterpart's peripheral-role name, which the host mode scans for.
static constexpr const char *TARGET_NAME = "EspBle MIDI Device 0106";

EspBleBluedroid bluetooth;
EspBleMidiDevice midiDevice(bluetooth);
EspBleMidiHost midiHost(bluetooth);
TaskHandle_t loopTask = nullptr;

char mode = 0;
bool started = false;
bool deviceRegistered = false;
bool connectRequested = false;
EspBleConnectionId connectionId = 0;

unsigned receivedCount = 0;
uint8_t lastStatus = 0;
uint8_t lastData1 = 0;
uint8_t lastData2 = 0;

unsigned sysExChunks = 0;
size_t sysExBytes = 0;
bool sysExStarted = false;
bool sysExEnded = false;
bool sysExRamp = true;

// 0xF0, the non-commercial ID 0x7D, a 96-byte ramp, 0xF7 — the same message the
// EspBle side sends, per the shared-expectations rule in tests/TEST_PLAN.md.
static constexpr size_t SYSEX_LENGTH = 99;
uint8_t sysExOut[SYSEX_LENGTH];
bool sysExPending = false;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
}

void resetCounters()
{
  receivedCount = 0;
  lastStatus = 0;
  lastData1 = 0;
  lastData2 = 0;
  sysExChunks = 0;
  sysExBytes = 0;
  sysExStarted = false;
  sysExEnded = false;
  sysExRamp = true;
}

void onMidi(const EspBleMidiMessage &message)
{
  if (message.sysEx)
  {
    ++sysExChunks;
    if (message.sysExStart)
    {
      sysExStarted = true;
      sysExBytes = 0;
      sysExRamp = true;
    }
    for (size_t index = 0; index < message.sysExLength; ++index)
    {
      const uint8_t expected = sysExBytes == 0
        ? 0x7D : static_cast<uint8_t>((sysExBytes - 1) & 0x7F);
      if (message.sysExData[index] != expected) sysExRamp = false;
      ++sysExBytes;
    }
    if (message.sysExEnd) sysExEnded = true;
    Serial.printf("BLUEDROID_MIDI_IN_SYSEX start=%u end=%u length=%u context=%s\n",
      message.sysExStart ? 1 : 0, message.sysExEnd ? 1 : 0,
      static_cast<unsigned>(message.sysExLength), contextName());
    return;
  }
  ++receivedCount;
  lastStatus = message.status;
  lastData1 = message.data1;
  lastData2 = message.data2;
  Serial.printf(
    "BLUEDROID_MIDI_IN status=0x%02x data1=%u data2=%u length=%u context=%s\n",
    message.status, message.data1, message.data2,
    static_cast<unsigned>(message.dataLength), contextName());
}

bool startWithMode(char requested)
{
  // The two directions share one flash, so switching role goes through end().
  if (started)
  {
    bluetooth.end();
    started = false;
  }
  connectRequested = false;
  connectionId = 0;
  resetCounters();

  EspBleConfig config;
  config.deviceName = requested == 'd' ? DEVICE_NAME : HOST_NAME;

  if (requested == 'd')
  {
    // Before begin(), and only once: the helper registers the MIDI service, its
    // characteristic and the advertised UUID.
    if (!deviceRegistered)
    {
      if (!midiDevice.begin())
      {
        Serial.printf("BLUEDROID_MIDI_BEGIN_FAILED %s\n",
          bluetooth.lastErrorName());
        return false;
      }
      deviceRegistered = true;
    }
    midiDevice.onMessage(onMidi);
    // The CCCD write arriving is what the other side's subscription looks like from
    // here, and it is the step a test has to wait for rather than poll. The helper
    // listens to this event too, so this is an additional observer, registered once
    // because end() does not forget listeners.
    static bool subscriptionListenerRegistered = false;
    if (!subscriptionListenerRegistered)
    {
      subscriptionListenerRegistered = true;
      bluetooth.gattServer().addSubscriptionChangedListener(
        [](const EspBleGattSubscription &subscription) {
          if (!subscription.characteristicUuid.equalsIgnoreCase(
                ESP_BLE_MIDI_IO_CHARACTERISTIC_UUID))
            return;
          Serial.printf("BLUEDROID_SUBSCRIPTION notifications=%u context=%s\n",
            subscription.notifications ? 1 : 0, contextName());
        });
    }
  }

  if (!bluetooth.begin(config))
  {
    Serial.printf("BLUEDROID_BEGIN_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return false;
  }
  started = true;
  mode = requested;

  if (requested == 'd')
  {
    // The name goes in the scan response: the helper already put the 128-bit MIDI
    // service UUID in the advertising payload, and a role-carrying name does not
    // fit beside it in 31 bytes.
    bluetooth.advertising().scanResponse().setName(DEVICE_NAME);
    if (!bluetooth.advertising().start())
    {
      Serial.printf("BLUEDROID_ADVERTISE_FAILED %s\n", bluetooth.lastErrorName());
      return false;
    }
    return true;
  }

  if (!midiHost.begin())
  {
    Serial.printf("BLUEDROID_MIDI_BEGIN_FAILED %s\n", bluetooth.lastErrorName());
    return false;
  }
  midiHost.onMidiMessage(onMidi);
  // Discovery and subscription are what `ready()` is made of, and they take a
  // round trip each: reported here so the test waits for them instead of polling,
  // and so a failure names the step that failed.
  static bool clientListenersRegistered = false;
  if (!clientListenersRegistered)
  {
    clientListenersRegistered = true;
    bluetooth.addCharacteristicDiscoveredListener(
      [](const EspBleGattResult &result) {
        if (!result.characteristicUuid.equalsIgnoreCase(
              ESP_BLE_MIDI_IO_CHARACTERISTIC_UUID))
          return;
        Serial.printf("BLUEDROID_DISCOVERED success=%u error=%s context=%s\n",
          result.success ? 1 : 0, bluetooth.lastErrorName(), contextName());
      });
    bluetooth.addSubscribedListener([](const EspBleGattResult &result) {
      if (!result.characteristicUuid.equalsIgnoreCase(
            ESP_BLE_MIDI_IO_CHARACTERISTIC_UUID))
        return;
      Serial.printf("BLUEDROID_SUBSCRIBED success=%u error=%s context=%s\n",
        result.success ? 1 : 0, bluetooth.lastErrorName(), contextName());
    });
  }
  // A central here runs one GATT operation at a time, so the test has to know when
  // a write has completed before issuing the next one. The helper listens to this
  // event too (it drives its own SysEx packets from it), so this is an additional
  // observer, registered once because `end()` does not forget listeners.
  static bool writeListenerRegistered = false;
  if (!writeListenerRegistered)
  {
    writeListenerRegistered = true;
    bluetooth.addCharacteristicWrittenListener(
      [](const EspBleGattResult &result) {
        if (midiHost.sendingSysEx()) return; // the helper is still pumping packets
        Serial.printf("BLUEDROID_WRITE_DONE success=%u context=%s\n",
          result.success ? 1 : 0, contextName());
      });
  }
  bluetooth.onConnected([](const EspBleConnection &connection) {
    if (mode != 'h') return; // a link the other side opened, in device mode
    connectionId = connection.id;
    Serial.printf("BLUEDROID_CONNECTED id=%u discover=%u context=%s\n",
      static_cast<unsigned>(connection.id),
      midiHost.discover(connection.id) ? 1 : 0, contextName());
  });
  bluetooth.onDisconnected([](const EspBleConnection &) { connectionId = 0; });
  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectRequested) return;
    if (!result.advertisesService(ESP_BLE_MIDI_SERVICE_UUID)) return;
    if (result.name != TARGET_NAME)
    {
      // A BLE MIDI advertiser that is not the counterpart. Reported because the
      // name is the only isolation here, so "found the service but not the name"
      // is the failure this scenario is most likely to hit.
      Serial.printf("BLUEDROID_SCAN_MIDI name=[%s] connectable=%u\n",
        result.name.c_str(), result.connectable ? 1 : 0);
      return;
    }
    connectRequested = true;
    bluetooth.scanner().stop();
    Serial.printf("BLUEDROID_TARGET_FOUND connect=%u\n",
      bluetooth.connect(result, 10000) ? 1 : 0);
  });
  return true;
}

bool ready()
{
  if (mode == 'd') return midiDevice.ready();
  return connectionId != 0 && midiHost.ready(connectionId);
}

bool sendMessage(const uint8_t *message, size_t length)
{
  if (mode == 'd') return midiDevice.sendMessage(message, length);
  return midiHost.sendMessage(connectionId, message, length);
}

bool sendSysEx()
{
  if (mode == 'd') return midiDevice.sendSysEx(sysExOut, SYSEX_LENGTH);
  return midiHost.sendSysEx(connectionId, sysExOut, SYSEX_LENGTH);
}

bool sendingSysEx()
{
  return mode == 'd' ? midiDevice.sendingSysEx() : midiHost.sendingSysEx();
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  loopTask = xTaskGetCurrentTaskHandle();
  sysExOut[0] = 0xF0;
  sysExOut[1] = 0x7D;
  for (size_t index = 2; index + 1 < SYSEX_LENGTH; ++index)
  {
    sysExOut[index] = static_cast<uint8_t>((index - 2) & 0x7F);
  }
  sysExOut[SYSEX_LENGTH - 1] = 0xF7;
  // begin() is deliberately not called here: the role comes from the mode command.
  Serial.println("BLUEDROID_MIDI_READY");
}

void loop()
{
  if (started) bluetooth.update();

  if (sysExPending && !sendingSysEx())
  {
    sysExPending = false;
    Serial.println("BLUEDROID_SYSEX_SENT");
  }

  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.printf("BLUEDROID_STATE mode=%c started=%u ready=%u\n",
        mode == 0 ? '-' : mode, started ? 1 : 0, ready() ? 1 : 0);
    }
    else if (command == 'd' || command == 'h')
    {
      const bool ok = startWithMode(command);
      Serial.printf("BLUEDROID_MODE_STARTED mode=%c success=%u error=%s\n",
        command, ok ? 1 : 0, bluetooth.lastErrorName());
    }
    else if (command == 'c')
    {
      Serial.printf("BLUEDROID_SCAN_STARTED %u\n",
        bluetooth.scanner().start() ? 1 : 0);
    }
    else if (command == 'n')
    {
      const uint8_t message[3] = {0x90, 0x3C, 0x64};
      Serial.printf("BLUEDROID_SEND accepted=%u\n",
        sendMessage(message, 3) ? 1 : 0);
    }
    else if (command == 'o')
    {
      const uint8_t message[3] = {0xB3, 0x07, 0x64};
      Serial.printf("BLUEDROID_SEND accepted=%u\n",
        sendMessage(message, 3) ? 1 : 0);
    }
    else if (command == 'g')
    {
      const uint8_t message[2] = {0xC9, 0x2A};
      Serial.printf("BLUEDROID_SEND accepted=%u\n",
        sendMessage(message, 2) ? 1 : 0);
    }
    else if (command == 's')
    {
      const bool accepted = sendSysEx();
      sysExPending = accepted;
      Serial.printf("BLUEDROID_SEND_SYSEX accepted=%u length=%u\n",
        accepted ? 1 : 0, static_cast<unsigned>(SYSEX_LENGTH));
    }
    else if (command == 'r')
    {
      Serial.printf(
        "BLUEDROID_RECEIVED messages=%u status=0x%02x data1=%u data2=%u\n",
        receivedCount, lastStatus, lastData1, lastData2);
      Serial.printf(
        "BLUEDROID_RECEIVED_SYSEX chunks=%u bytes=%u start=%u end=%u ramp=%u\n",
        sysExChunks, static_cast<unsigned>(sysExBytes), sysExStarted ? 1 : 0,
        sysExEnded ? 1 : 0, sysExRamp ? 1 : 0);
    }
  }
  delay(1);
}
