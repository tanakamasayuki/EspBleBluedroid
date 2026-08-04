// The EspBle (NimBLE) half of the cross-stack BLE MIDI scenario, running on an
// ESP32-S3 against the released EspBle pinned in sketch.yaml.
//
// `EspBleMidi.h` is byte-identical in both libraries and `EspBleMidiProfile.h`
// differs only in the type of the library reference, both of which are already
// machine-checked (tests/unit/midi, tests/unit/api_parity). What no diff can show
// is whether the two stacks agree on the *transport* underneath: the CCCD write,
// notifications against a negotiated MTU, Write Without Response, and how a SysEx
// that spans packets survives a different link-layer. That is what this suite
// exercises, with each side encoding and decoding through its own library.
//
// One firmware serves both directions. The mode command chooses whether this
// board is the MIDI Device (peripheral) or the MIDI Host (central), and the role
// is part of the advertised name so the other side never latches onto the wrong
// one: the BLE MIDI UUIDs are fixed by the specification, so a suite-tag UUID
// cannot be used for isolation here (tests/TEST_PLAN.md).
//
// Output is prefixed ESPBLE_ so a log line never leaves it ambiguous which stack
// produced it.

#include <EspBle.h>
#include <EspBleMidiProfile.h>

static constexpr const char *DEVICE_NAME = "EspBle MIDI Device 0106";
static constexpr const char *HOST_NAME = "EspBle MIDI Host 0106";
// The counterpart's peripheral-role name, which the host mode scans for.
static constexpr const char *TARGET_NAME = "Bluedroid MIDI Device 0106";

EspBle ble;
EspBleMidiDevice midiDevice(ble);
EspBleMidiHost midiHost(ble);

char mode = 0;
bool started = false;
bool deviceRegistered = false;
bool connectRequested = false;
EspBleConnectionId connectionId = 0;

// Received channel/system messages, reported on request.
unsigned receivedCount = 0;
uint8_t lastStatus = 0;
uint8_t lastData1 = 0;
uint8_t lastData2 = 0;

// Received SysEx, reassembled from the chunks the parser delivers. The payload is
// checked against the ramp the other side sent rather than dumped, so a reordered
// or dropped packet is visible in one flag.
unsigned sysExChunks = 0;
size_t sysExBytes = 0;
bool sysExStarted = false;
bool sysExEnded = false;
bool sysExRamp = true;

// 0xF0, the non-commercial ID 0x7D, a 96-byte ramp, 0xF7.
static constexpr size_t SYSEX_LENGTH = 99;
uint8_t sysExOut[SYSEX_LENGTH];
bool sysExPending = false;

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
    Serial.printf("ESPBLE_MIDI_IN_SYSEX start=%u end=%u length=%u\n",
      message.sysExStart ? 1 : 0, message.sysExEnd ? 1 : 0,
      static_cast<unsigned>(message.sysExLength));
    return;
  }
  ++receivedCount;
  lastStatus = message.status;
  lastData1 = message.data1;
  lastData2 = message.data2;
  Serial.printf("ESPBLE_MIDI_IN status=0x%02x data1=%u data2=%u length=%u\n",
    message.status, message.data1, message.data2,
    static_cast<unsigned>(message.dataLength));
}

bool startWithMode(char requested)
{
  // The two directions share one flash, so switching role goes through end():
  // the device role registers a GATT service and advertises, the host role scans.
  if (started)
  {
    ble.end();
    started = false;
  }
  connectRequested = false;
  connectionId = 0;
  resetCounters();

  EspBleConfig config;
  config.deviceName = requested == 'd' ? DEVICE_NAME : HOST_NAME;

  if (requested == 'd')
  {
    // Before begin(): the helper registers the MIDI service, its characteristic
    // and the advertised UUID. Registering twice across a mode switch would add a
    // second service, so it is done once.
    if (!deviceRegistered)
    {
      if (!midiDevice.begin())
      {
        Serial.printf("ESPBLE_MIDI_BEGIN_FAILED %s\n", ble.lastErrorName());
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
      ble.gattServer().addSubscriptionChangedListener(
        [](const EspBleGattSubscription &subscription) {
          if (!subscription.characteristicUuid.equalsIgnoreCase(
                ESP_BLE_MIDI_IO_CHARACTERISTIC_UUID))
            return;
          Serial.printf("ESPBLE_SUBSCRIPTION notifications=%u\n",
            subscription.notifications ? 1 : 0);
        });
    }
  }

  if (!ble.begin(config))
  {
    Serial.printf("ESPBLE_BEGIN_FAILED %s %s\n", ble.lastErrorName(),
      ble.lastErrorDetail().c_str());
    return false;
  }
  started = true;
  mode = requested;

  if (requested == 'd')
  {
    // The name goes in the scan response, not the advertising payload: the helper
    // already put the 128-bit MIDI service UUID there (18 of the 31 bytes), and a
    // role-carrying name does not fit beside it. Without this the other side's
    // scanner sees the service but no name, and name is the only isolation this
    // scenario has.
    ble.advertising().scanResponse().setName(DEVICE_NAME);
    if (!ble.advertising().start())
    {
      Serial.printf("ESPBLE_ADVERTISE_FAILED %s\n", ble.lastErrorName());
      return false;
    }
    return true;
  }

  // Host role: install the GATT client listeners after begin(), then scan.
  if (!midiHost.begin())
  {
    Serial.printf("ESPBLE_MIDI_BEGIN_FAILED %s\n", ble.lastErrorName());
    return false;
  }
  midiHost.onMidiMessage(onMidi);
  // Discovery and subscription are what `ready()` is made of, and they take a
  // round trip each: reported here so the test waits for them instead of polling,
  // and so a failure names the step that failed. Additional observers again --
  // the helper drives the subscription from the same discovery event.
  static bool clientListenersRegistered = false;
  if (!clientListenersRegistered)
  {
    clientListenersRegistered = true;
    ble.addCharacteristicDiscoveredListener([](const EspBleGattResult &result) {
      if (!result.characteristicUuid.equalsIgnoreCase(
            ESP_BLE_MIDI_IO_CHARACTERISTIC_UUID))
        return;
      Serial.printf("ESPBLE_DISCOVERED success=%u error=%s\n",
        result.success ? 1 : 0, ble.lastErrorName());
    });
    ble.addSubscribedListener([](const EspBleGattResult &result) {
      if (!result.characteristicUuid.equalsIgnoreCase(
            ESP_BLE_MIDI_IO_CHARACTERISTIC_UUID))
        return;
      Serial.printf("ESPBLE_SUBSCRIBED success=%u error=%s\n",
        result.success ? 1 : 0, ble.lastErrorName());
    });
  }
  ble.onConnected([](const EspBleConnection &connection) {
    if (mode != 'h') return; // a link someone else opened, in device mode
    connectionId = connection.id;
    Serial.printf("ESPBLE_CONNECTED id=%u discover=%u\n",
      static_cast<unsigned>(connection.id),
      midiHost.discover(connection.id) ? 1 : 0);
  });
  ble.onDisconnected([](const EspBleConnection &) { connectionId = 0; });
  ble.scanner().onResult([](const EspBleScanResult &result) {
    if (connectRequested || result.name != TARGET_NAME) return;
    if (!result.advertisesService(ESP_BLE_MIDI_SERVICE_UUID)) return;
    connectRequested = true;
    ble.scanner().stop();
    Serial.printf("ESPBLE_TARGET_FOUND connect=%u\n",
      ble.connect(result, 10000) ? 1 : 0);
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
  sysExOut[0] = 0xF0;
  sysExOut[1] = 0x7D;
  for (size_t index = 2; index + 1 < SYSEX_LENGTH; ++index)
  {
    sysExOut[index] = static_cast<uint8_t>((index - 2) & 0x7F);
  }
  sysExOut[SYSEX_LENGTH - 1] = 0xF7;
  // begin() is deliberately not called here: the role comes from the mode command.
  Serial.println("ESPBLE_MIDI_READY");
}

void loop()
{
  if (started) ble.update();

  if (sysExPending && !sendingSysEx())
  {
    sysExPending = false;
    Serial.println("ESPBLE_SYSEX_SENT");
  }

  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.printf("ESPBLE_STATE mode=%c started=%u ready=%u\n",
        mode == 0 ? '-' : mode, started ? 1 : 0, ready() ? 1 : 0);
    }
    else if (command == 'd' || command == 'h')
    {
      const bool ok = startWithMode(command);
      Serial.printf("ESPBLE_MODE_STARTED mode=%c success=%u error=%s\n", command,
        ok ? 1 : 0, ble.lastErrorName());
    }
    else if (command == 'c')
    {
      Serial.printf("ESPBLE_SCAN_STARTED %u\n", ble.scanner().start() ? 1 : 0);
    }
    else if (command == 'n')
    {
      const uint8_t message[3] = {0x90, 0x3C, 0x64};
      Serial.printf("ESPBLE_SEND accepted=%u\n", sendMessage(message, 3) ? 1 : 0);
    }
    else if (command == 'o')
    {
      const uint8_t message[3] = {0xB3, 0x07, 0x64};
      Serial.printf("ESPBLE_SEND accepted=%u\n", sendMessage(message, 3) ? 1 : 0);
    }
    else if (command == 'g')
    {
      const uint8_t message[2] = {0xC9, 0x2A};
      Serial.printf("ESPBLE_SEND accepted=%u\n", sendMessage(message, 2) ? 1 : 0);
    }
    else if (command == 's')
    {
      const bool accepted = sendSysEx();
      sysExPending = accepted;
      Serial.printf("ESPBLE_SEND_SYSEX accepted=%u length=%u\n", accepted ? 1 : 0,
        static_cast<unsigned>(SYSEX_LENGTH));
    }
    else if (command == 'r')
    {
      Serial.printf(
        "ESPBLE_RECEIVED messages=%u status=0x%02x data1=%u data2=%u\n",
        receivedCount, lastStatus, lastData1, lastData2);
      Serial.printf(
        "ESPBLE_RECEIVED_SYSEX chunks=%u bytes=%u start=%u end=%u ramp=%u\n",
        sysExChunks, static_cast<unsigned>(sysExBytes), sysExStarted ? 1 : 0,
        sysExEnded ? 1 : 0, sysExRamp ? 1 : 0);
    }
  }
  delay(1);
}
