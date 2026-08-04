// BLE MIDI Host (central) against a raw Arduino-ESP32 BLE MIDI peripheral.
//
// The peer stands in for an instrument and notifies packets it built by hand, so
// what is verified is the parser against the specification: running status
// carried across a packet, a System Real-Time byte interleaved between two
// messages, and a SysEx spread over three notifications. In the other direction
// the peer decodes what this sketch writes, which pins the encoder.
//
// One Bluedroid property shows through and is part of what the test fixes: a
// central runs one GATT operation at a time, so consecutive sends have to be
// chained through the write completion. The completion is observed with
// add*Listener(), because EspBleMidiHost itself is already listening to it.
//
// The BLE MIDI UUIDs come from the specification, so isolation from a test on a
// neighbouring bench is by device name.

#include <EspBleBluedroid.h>
#include <EspBleMidiProfile.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *TARGET_NAME = "Bluedroid MIDI Peer 000a";

EspBleBluedroid bluetooth;
EspBleMidiHost midi(bluetooth);
TaskHandle_t loopTask = nullptr;
bool started = false;
bool connectRequested = false;
EspBleConnectionId deviceConnectionId = 0;

// Received channel/system messages, reported on request.
unsigned receivedCount = 0;
uint8_t lastStatus = 0;
uint8_t lastData1 = 0;
uint8_t lastData2 = 0;

// Received SysEx, reassembled from the chunks the parser delivers.
unsigned sysExChunks = 0;
size_t sysExBytes = 0;
bool sysExStarted = false;
bool sysExEnded = false;
bool sysExRamp = true;

// A SysEx long enough to need more than one write. The library raises the MTU to
// 247 when it connects, so one ATT write carries 244 bytes: 320 is the smallest
// round size above that, and it is the helper's own maximum.
static constexpr size_t SYSEX_LENGTH = 320;
uint8_t sysExOut[SYSEX_LENGTH];
bool sysExPending = false;

const char *contextName()
{
  return xTaskGetCurrentTaskHandle() == loopTask ? "loop" : "stack";
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

  midi.onMidiMessage([](const EspBleMidiMessage &message) {
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
      Serial.printf(
        "MIDI_IN_SYSEX id=%u start=%u end=%u length=%u context=%s\n",
        static_cast<unsigned>(message.connectionId),
        message.sysExStart ? 1 : 0, message.sysExEnd ? 1 : 0,
        static_cast<unsigned>(message.sysExLength), contextName());
      return;
    }
    ++receivedCount;
    lastStatus = message.status;
    lastData1 = message.data1;
    lastData2 = message.data2;
    Serial.printf(
      "MIDI_IN id=%u status=0x%02x data1=%u data2=%u length=%u ts=%u context=%s\n",
      static_cast<unsigned>(message.connectionId), message.status,
      message.data1, message.data2,
      static_cast<unsigned>(message.dataLength), message.timestampMs,
      contextName());
  });

  EspBleConfig config;
  config.deviceName = "Bluedroid MIDI Host 000a";
  if (!bluetooth.begin(config))
  {
    Serial.printf("BEGIN_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }

  // After begin(): the helper installs the GATT client listeners it needs.
  if (!midi.begin())
  {
    Serial.printf("MIDI_BEGIN_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }

  // The helper listens to this event too, to drive its own SysEx packets. This is
  // an additional observer, so both see it.
  bluetooth.addCharacteristicWrittenListener([](const EspBleGattResult &result) {
    if (midi.sendingSysEx()) return; // the helper is still pumping packets
    Serial.printf("WRITE_DONE success=%u context=%s\n", result.success ? 1 : 0,
      contextName());
  });

  bluetooth.onConnected([](const EspBleConnection &connection) {
    deviceConnectionId = connection.id;
    Serial.printf("CONNECTED id=%u discover=%u context=%s\n",
      static_cast<unsigned>(connection.id),
      midi.discover(connection.id) ? 1 : 0, contextName());
  });
  bluetooth.onDisconnected([](const EspBleConnection &) {
    deviceConnectionId = 0;
    Serial.println("DISCONNECTED");
  });

  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (connectRequested || result.name != TARGET_NAME) return;
    if (!result.advertisesService(ESP_BLE_MIDI_SERVICE_UUID)) return;
    connectRequested = true;
    bluetooth.scanner().stop();
    Serial.printf("TARGET_FOUND connect=%u\n",
      bluetooth.connect(result, 10000) ? 1 : 0);
  });
  started = true;
  Serial.println("MIDI_HOST_READY");
}

void loop()
{
  bluetooth.update();

  // Set when a transfer is accepted, cleared when it has drained. Deliberately
  // not an edge detected around update(): every packet after the first is sent
  // from a write completion, and one update() can drain them all, so a "was
  // sending last time round" test would never see the flag set.
  if (sysExPending && !midi.sendingSysEx())
  {
    sysExPending = false;
    Serial.println("SYSEX_SENT");
  }

  if (Serial.available())
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.printf("READY_STATE started=%u id=%u ready=%u\n", started ? 1 : 0,
        static_cast<unsigned>(deviceConnectionId),
        midi.ready(deviceConnectionId) ? 1 : 0);
    }
    else if (command == 'c')
    {
      Serial.printf("SCAN_STARTED %u\n", bluetooth.scanner().start() ? 1 : 0);
    }
    else if (command == 'n')
    {
      Serial.printf("SEND accepted=%u\n",
        midi.sendNoteOn(deviceConnectionId, 0, 60, 100) ? 1 : 0);
    }
    else if (command == 'f')
    {
      Serial.printf("SEND accepted=%u\n",
        midi.sendNoteOff(deviceConnectionId, 0, 60, 0) ? 1 : 0);
    }
    else if (command == 'o')
    {
      Serial.printf("SEND accepted=%u\n",
        midi.sendControlChange(deviceConnectionId, 3, 7, 100) ? 1 : 0);
    }
    else if (command == 'g')
    {
      Serial.printf("SEND accepted=%u\n",
        midi.sendProgramChange(deviceConnectionId, 9, 42) ? 1 : 0);
    }
    else if (command == 's')
    {
      const bool accepted =
        midi.sendSysEx(deviceConnectionId, sysExOut, SYSEX_LENGTH);
      // Both refusals are checked while the first transfer is certain to still be
      // running: a second SysEx, and a plain message that would cut into it.
      const bool second =
        midi.sendSysEx(deviceConnectionId, sysExOut, SYSEX_LENGTH);
      const bool note = midi.sendNoteOn(deviceConnectionId, 0, 60, 100);
      sysExPending = accepted;
      Serial.printf(
        "SEND_SYSEX accepted=%u length=%u second=%u note=%u sending=%u\n",
        accepted ? 1 : 0, static_cast<unsigned>(SYSEX_LENGTH),
        second ? 1 : 0, note ? 1 : 0, midi.sendingSysEx() ? 1 : 0);
    }
    else if (command == 'r')
    {
      Serial.printf("RECEIVED messages=%u status=0x%02x data1=%u data2=%u\n",
        receivedCount, lastStatus, lastData1, lastData2);
      Serial.printf(
        "RECEIVED_SYSEX chunks=%u bytes=%u start=%u end=%u ramp=%u\n",
        sysExChunks, static_cast<unsigned>(sysExBytes), sysExStarted ? 1 : 0,
        sysExEnded ? 1 : 0, sysExRamp ? 1 : 0);
    }
    else if (command == 'x')
    {
      Serial.printf("DISCONNECT_REQUESTED %u\n",
        bluetooth.disconnect(deviceConnectionId) ? 1 : 0);
    }
  }
  delay(1);
}
