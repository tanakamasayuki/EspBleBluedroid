// A raw Arduino-ESP32 BLE central for the BLE MIDI Device scenario.
//
// Deliberately not this library and not its codec: the header/timestamp
// arithmetic below is written out by hand from the MMA/Apple "MIDI over
// Bluetooth Low Energy 1.0" wire format, so the bytes the library emits are
// checked against the specification rather than against themselves. The packets
// this sketch writes are hand-built for the same reason.

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLERemoteCharacteristic.h>

static constexpr const char *SERVICE_UUID =
  "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static constexpr const char *CHARACTERISTIC_UUID =
  "7772E5DB-3868-4112-A1A9-F2669D106BF3";
// The MIDI UUIDs are fixed by the specification, so the device name is what keeps
// this test from latching onto an unrelated board on a neighbouring bench.
static constexpr const char *TARGET_NAME = "Bluedroid MIDI 0009";

BLEClient *client = nullptr;
BLERemoteCharacteristic *characteristic = nullptr;

// Decoded notifications, reported on request.
unsigned notifyCount = 0;
unsigned headerFailures = 0;
unsigned sysExPackets = 0;
size_t sysExBytes = 0;
bool sysExStarted = false;
bool sysExEnded = false;
bool sysExRamp = true;
bool inSysEx = false;

uint16_t timestamp() { return static_cast<uint16_t>(millis() & 0x1FFF); }

void appendHeader(uint8_t *packet, size_t &length)
{
  const uint16_t now = timestamp();
  packet[length++] = static_cast<uint8_t>(0x80 | ((now >> 7) & 0x3F));
  packet[length++] = static_cast<uint8_t>(0x80 | (now & 0x7F));
}

void writePacket(const uint8_t *packet, size_t length, const char *tag)
{
  if (characteristic == nullptr)
  {
    Serial.println("PEER_NOT_CONNECTED");
    return;
  }
  // Write Without Response: what a BLE MIDI host uses.
  characteristic->writeValue(const_cast<uint8_t *>(packet), length, false);
  Serial.printf("PEER_WROTE %s length=%u\n", tag,
    static_cast<unsigned>(length));
}

// Accumulate the SysEx payload carried by one packet, starting at `index`.
void collectSysEx(const uint8_t *data, size_t length, size_t index)
{
  for (; index < length; ++index)
  {
    const uint8_t value = data[index];
    if (value == 0xF7)
    {
      sysExEnded = true;
      inSysEx = false;
      return;
    }
    // A byte with bit 7 set inside a SysEx is a timestamp (the specification
    // allows one before 0xF7 and before an interleaved real-time message).
    if ((value & 0x80) != 0) continue;
    const uint8_t expected = sysExBytes == 0
      ? 0x7D : static_cast<uint8_t>((sysExBytes - 1) & 0x7F);
    if (value != expected) sysExRamp = false;
    ++sysExBytes;
  }
}

void notificationCallback(
  BLERemoteCharacteristic *, uint8_t *data, size_t length, bool)
{
  ++notifyCount;
  // Every packet starts with a header byte: bit7 set, bit6 clear.
  if (length < 1 || (data[0] & 0xC0) != 0x80)
  {
    ++headerFailures;
    Serial.printf("PEER_NOTIFY_BAD_HEADER length=%u\n",
      static_cast<unsigned>(length));
    return;
  }
  const uint16_t timestampHigh = static_cast<uint16_t>(data[0] & 0x3F);

  if (inSysEx)
  {
    ++sysExPackets;
    collectSysEx(data, length, 1);
    Serial.printf("PEER_SYSEX_PACKET continuation=1 length=%u\n",
      static_cast<unsigned>(length));
    return;
  }

  // A message packet: header, timestamp, then the MIDI bytes.
  if (length < 3 || (data[1] & 0x80) == 0)
  {
    ++headerFailures;
    Serial.printf("PEER_NOTIFY_BAD_TIMESTAMP length=%u\n",
      static_cast<unsigned>(length));
    return;
  }
  const uint16_t stamp =
    static_cast<uint16_t>((timestampHigh << 7) | (data[1] & 0x7F));

  if (data[2] == 0xF0)
  {
    inSysEx = true;
    sysExStarted = true;
    ++sysExPackets;
    sysExBytes = 0;
    sysExRamp = true;
    sysExEnded = false;
    collectSysEx(data, length, 3);
    Serial.printf("PEER_SYSEX_PACKET continuation=0 length=%u ts=%u\n",
      static_cast<unsigned>(length), stamp);
    return;
  }

  Serial.printf("PEER_NOTIFY length=%u ts=%u midi=",
    static_cast<unsigned>(length), stamp);
  for (size_t index = 2; index < length; ++index)
  {
    Serial.printf("%02x", data[index]);
  }
  Serial.println();
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init("Bluedroid MIDI Peer");
  Serial.println("MIDI_DEVICE_PEER_READY");
}

void loop()
{
  if (!Serial.available())
  {
    delay(1);
    return;
  }
  const int command = Serial.read();
  if (command == 'c')
  {
    BLEScan *scan = BLEDevice::getScan();
    scan->setActiveScan(true);
    BLEScanResults *results = scan->start(5, false);
    BLEAdvertisedDevice *target = nullptr;
    for (int index = 0; results != nullptr && index < results->getCount();
         ++index)
    {
      BLEAdvertisedDevice device = results->getDevice(index);
      if (device.haveServiceUUID() &&
          device.isAdvertisingService(BLEUUID(SERVICE_UUID)) &&
          device.haveName() && device.getName() == String(TARGET_NAME))
      {
        target = new BLEAdvertisedDevice(device);
        break;
      }
    }
    if (target == nullptr)
    {
      Serial.println("PEER_TARGET_NOT_FOUND");
      return;
    }
    client = BLEDevice::createClient();
    if (!client->connect(target))
    {
      Serial.println("PEER_CONNECT_FAILED");
      return;
    }
    BLERemoteService *service = client->getService(SERVICE_UUID);
    characteristic = service == nullptr
      ? nullptr : service->getCharacteristic(CHARACTERISTIC_UUID);
    Serial.printf("PEER_CONNECTED characteristic=%u\n",
      characteristic != nullptr ? 1 : 0);
  }
  else if (command == 's')
  {
    if (characteristic == nullptr)
    {
      Serial.println("PEER_NOT_CONNECTED");
      return;
    }
    characteristic->registerForNotify(notificationCallback, true);
    Serial.println("PEER_SUBSCRIBED");
  }
  else if (command == 'n')
  {
    // One Note On, the plainest packet there is.
    uint8_t packet[8];
    size_t length = 0;
    appendHeader(packet, length);
    packet[length++] = 0x90;
    packet[length++] = 0x3C;
    packet[length++] = 0x64;
    writePacket(packet, length, "note_on");
  }
  else if (command == 'r')
  {
    // Running status: two Note Ons sharing one status byte, which the receiving
    // parser has to carry over.
    uint8_t packet[8];
    size_t length = 0;
    appendHeader(packet, length);
    packet[length++] = 0x90;
    packet[length++] = 0x3C;
    packet[length++] = 0x64;
    packet[length++] = 0x3E;
    packet[length++] = 0x65;
    writePacket(packet, length, "running_status");
  }
  else if (command == 'y')
  {
    // A SysEx across three writes: 0x7D then a 39-byte ramp. The middle write
    // carries no timestamp, and the last one has one before 0xF7.
    //
    // No write exceeds 20 bytes: nothing raises the MTU on this link, so an ATT
    // write carries at most MTU - 3 bytes and a longer one would be truncated
    // here, before it ever reached the air. The device under test is left at the
    // default MTU on purpose — that is what makes its own SysEx span six
    // notifications instead of one.
    uint8_t packet[32];
    size_t length = 0;
    appendHeader(packet, length);
    packet[length++] = 0xF0;
    packet[length++] = 0x7D;
    for (uint8_t value = 0; value < 10; ++value) packet[length++] = value;
    writePacket(packet, length, "sysex_first");
    delay(50);

    length = 0;
    const uint16_t now = timestamp();
    packet[length++] = static_cast<uint8_t>(0x80 | ((now >> 7) & 0x3F));
    for (uint8_t value = 10; value < 29; ++value) packet[length++] = value;
    writePacket(packet, length, "sysex_continuation");
    delay(50);

    length = 0;
    packet[length++] = static_cast<uint8_t>(0x80 | ((now >> 7) & 0x3F));
    for (uint8_t value = 29; value < 39; ++value) packet[length++] = value;
    packet[length++] = static_cast<uint8_t>(0x80 | (now & 0x7F));
    packet[length++] = 0xF7;
    writePacket(packet, length, "sysex_last");
  }
  else if (command == 'q')
  {
    Serial.printf("PEER_REPORT notifications=%u header_failures=%u\n",
      notifyCount, headerFailures);
    Serial.printf(
      "PEER_SYSEX packets=%u bytes=%u start=%u end=%u ramp=%u\n",
      sysExPackets, static_cast<unsigned>(sysExBytes), sysExStarted ? 1 : 0,
      sysExEnded ? 1 : 0, sysExRamp ? 1 : 0);
  }
  else if (command == 'x')
  {
    if (client != nullptr) client->disconnect();
    Serial.println("PEER_DISCONNECT_REQUESTED");
  }
}
