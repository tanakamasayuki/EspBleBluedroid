// BLE MIDI Device (peripheral) against a raw Arduino-ESP32 central.
//
// What this fixes is the wire format, not the API: the peer decodes the BLE MIDI
// header/timestamp bytes with its own few lines of arithmetic and prints the MIDI
// bytes it recovered, so a change in EspBleMidiProfile.h or EspBleMidi.h that
// still compiles but puts different bytes on the air fails here. The same in
// reverse: the peer writes packets it built by hand (including running status and
// a SysEx split across three writes) and this sketch reports what the parser made
// of them.
//
// The BLE MIDI service and characteristic UUIDs come from the specification, so
// unlike the other suites this one cannot pick unused UUIDs. Isolation from a
// test running on a neighbouring bench is by device name instead, and the peer
// requires both the name and the service UUID to match.

#include <EspBleBluedroid.h>
#include <EspBleMidiProfile.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static constexpr const char *DEVICE_NAME = "Bluedroid MIDI 0009";

EspBleBluedroid bluetooth;
EspBleMidiDevice midi(bluetooth);
TaskHandle_t loopTask = nullptr;
bool started = false;

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

// A SysEx long enough to need several notifications at the default 23-byte MTU:
// 0xF0, the non-commercial ID 0x7D, 96 ramp bytes, 0xF7.
static constexpr size_t SYSEX_LENGTH = 99;
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

  midi.onMessage([](const EspBleMidiMessage &message) {
    if (message.sysEx)
    {
      ++sysExChunks;
      if (message.sysExStart)
      {
        sysExStarted = true;
        sysExBytes = 0;
        sysExRamp = true;
      }
      // The peer writes 0x7D followed by a ramp, so the payload is checked here
      // rather than dumped: a reordered or dropped chunk changes it.
      for (size_t index = 0; index < message.sysExLength; ++index)
      {
        const uint8_t expected = sysExBytes == 0
          ? 0x7D : static_cast<uint8_t>((sysExBytes - 1) & 0x7F);
        if (message.sysExData[index] != expected) sysExRamp = false;
        ++sysExBytes;
      }
      if (message.sysExEnd) sysExEnded = true;
      Serial.printf("MIDI_IN_SYSEX start=%u end=%u length=%u context=%s\n",
        message.sysExStart ? 1 : 0, message.sysExEnd ? 1 : 0,
        static_cast<unsigned>(message.sysExLength), contextName());
      return;
    }
    ++receivedCount;
    lastStatus = message.status;
    lastData1 = message.data1;
    lastData2 = message.data2;
    Serial.printf(
      "MIDI_IN status=0x%02x data1=%u data2=%u length=%u ts=%u context=%s\n",
      message.status, message.data1, message.data2,
      static_cast<unsigned>(message.dataLength), message.timestampMs,
      contextName());
  });

  // Before begin(): the service, its characteristic and the advertised UUID are
  // registered by the helper.
  if (!midi.begin())
  {
    Serial.printf("MIDI_BEGIN_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }

  EspBleConfig config;
  config.deviceName = DEVICE_NAME;
  if (!bluetooth.begin(config))
  {
    Serial.printf("BEGIN_FAILED %s %s\n", bluetooth.lastErrorName(),
      bluetooth.lastErrorDetail().c_str());
    return;
  }
  if (!bluetooth.advertising().start())
  {
    Serial.printf("ADVERTISE_FAILED %s\n", bluetooth.lastErrorName());
    return;
  }
  started = true;
  Serial.println("MIDI_DEVICE_READY");
}

void loop()
{
  bluetooth.update();

  // Set when a transfer is accepted, cleared when it has drained. Deliberately
  // not an edge detected around update(): every packet after the first is sent
  // from a send completion, and one update() can drain them all, so a "was
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
      // Requested rather than printed at boot: the first output is lost while
      // the other board is flashed.
      Serial.printf("READY_STATE started=%u ready=%u\n", started ? 1 : 0,
        midi.ready() ? 1 : 0);
    }
    else if (command == 'n')
    {
      Serial.printf("SEND accepted=%u\n", midi.noteOn(0, 60, 100) ? 1 : 0);
    }
    else if (command == 'f')
    {
      Serial.printf("SEND accepted=%u\n", midi.noteOff(0, 60, 0) ? 1 : 0);
    }
    else if (command == 'c')
    {
      Serial.printf("SEND accepted=%u\n",
        midi.controlChange(3, 7, 100) ? 1 : 0);
    }
    else if (command == 'p')
    {
      // 0x2000 is centre; the codec must split it into two 7-bit halves.
      Serial.printf("SEND accepted=%u\n", midi.pitchBend(0, 0x2000) ? 1 : 0);
    }
    else if (command == 'g')
    {
      Serial.printf("SEND accepted=%u\n", midi.programChange(9, 42) ? 1 : 0);
    }
    else if (command == 's')
    {
      const bool accepted = midi.sendSysEx(sysExOut, SYSEX_LENGTH);
      // The second attempt is made here rather than from a later command so the
      // first transfer is guaranteed to still be in flight: a SysEx must be
      // refused while one is running, not interleaved with it. Same for a plain
      // message, which would otherwise cut into the stream.
      const bool second = midi.sendSysEx(sysExOut, SYSEX_LENGTH);
      const bool note = midi.noteOn(0, 60, 100);
      sysExPending = accepted;
      Serial.printf(
        "SEND_SYSEX accepted=%u length=%u second=%u note=%u sending=%u\n",
        accepted ? 1 : 0, static_cast<unsigned>(SYSEX_LENGTH),
        second ? 1 : 0, note ? 1 : 0, midi.sendingSysEx() ? 1 : 0);
    }
    else if (command == 'r')
    {
      Serial.printf(
        "RECEIVED messages=%u status=0x%02x data1=%u data2=%u\n",
        receivedCount, lastStatus, lastData1, lastData2);
      Serial.printf(
        "RECEIVED_SYSEX chunks=%u bytes=%u start=%u end=%u ramp=%u\n",
        sysExChunks, static_cast<unsigned>(sysExBytes), sysExStarted ? 1 : 0,
        sysExEnded ? 1 : 0, sysExRamp ? 1 : 0);
    }
  }
  delay(1);
}
