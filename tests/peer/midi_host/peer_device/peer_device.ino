// A raw Arduino-ESP32 BLE MIDI peripheral for the BLE MIDI Host scenario: it
// stands in for an instrument. The packets it notifies are built by hand from the
// MMA/Apple "MIDI over Bluetooth Low Energy 1.0" wire format — including running
// status, an interleaved System Real-Time byte and a SysEx split across three
// notifications — so what the library's parser produces is compared against the
// specification rather than against the same codec twice.

#include <BLEDevice.h>
#include <BLE2902.h>
#include <BLEServer.h>

static constexpr const char *SERVICE_UUID =
  "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static constexpr const char *CHARACTERISTIC_UUID =
  "7772E5DB-3868-4112-A1A9-F2669D106BF3";
// The MIDI UUIDs are fixed by the specification, so the name is what keeps the
// host under test from latching onto a board on a neighbouring bench.
static constexpr const char *DEVICE_NAME = "Bluedroid MIDI Peer 000a";

BLECharacteristic *characteristic = nullptr;
bool connected = false;
bool subscribed = false;
unsigned writeCount = 0;
String lastWrite;

// Writes from the host under test, reassembled the same way.
unsigned sysExPackets = 0;
size_t sysExBytes = 0;
bool sysExStarted = false;
bool sysExEnded = false;
bool sysExRamp = true;
bool inSysEx = false;

uint16_t timestamp() { return static_cast<uint16_t>(millis() & 0x1FFF); }

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
    if ((value & 0x80) != 0) continue; // timestamp byte
    const uint8_t expected = sysExBytes == 0
      ? 0x7D : static_cast<uint8_t>((sysExBytes - 1) & 0x7F);
    if (value != expected) sysExRamp = false;
    ++sysExBytes;
  }
}

class CharacteristicCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *target) override
  {
    const String value = target->getValue();
    const uint8_t *data = reinterpret_cast<const uint8_t *>(value.c_str());
    const size_t length = value.length();
    ++writeCount;
    lastWrite = "";
    for (size_t index = 0; index < length; ++index)
    {
      char byteText[3];
      snprintf(byteText, sizeof(byteText), "%02x", data[index]);
      lastWrite += byteText;
    }
    if (length < 1 || (data[0] & 0xC0) != 0x80)
    {
      Serial.printf("PEER_WRITE_BAD_HEADER length=%u hex=%s\n",
        static_cast<unsigned>(length), lastWrite.c_str());
      return;
    }
    if (inSysEx)
    {
      ++sysExPackets;
      collectSysEx(data, length, 1);
      Serial.printf("PEER_WRITE_SYSEX continuation=1 length=%u\n",
        static_cast<unsigned>(length));
      return;
    }
    if (length < 3 || (data[1] & 0x80) == 0)
    {
      Serial.printf("PEER_WRITE_BAD_TIMESTAMP length=%u hex=%s\n",
        static_cast<unsigned>(length), lastWrite.c_str());
      return;
    }
    const uint16_t stamp = static_cast<uint16_t>(
      ((data[0] & 0x3F) << 7) | (data[1] & 0x7F));
    if (data[2] == 0xF0)
    {
      inSysEx = true;
      sysExStarted = true;
      sysExEnded = false;
      ++sysExPackets;
      sysExBytes = 0;
      sysExRamp = true;
      collectSysEx(data, length, 3);
      Serial.printf("PEER_WRITE_SYSEX continuation=0 length=%u\n",
        static_cast<unsigned>(length));
      return;
    }
    Serial.printf("PEER_WRITE length=%u ts=%u midi=%s\n",
      static_cast<unsigned>(length), stamp, lastWrite.c_str() + 4);
  }
};

class DescriptorCallbacks : public BLEDescriptorCallbacks
{
  void onWrite(BLEDescriptor *descriptor) override
  {
    const uint8_t *value = descriptor->getValue();
    subscribed = descriptor->getLength() >= 1 && (value[0] & 0x01) != 0;
    Serial.printf("PEER_SUBSCRIPTION notifications=%u\n", subscribed ? 1 : 0);
  }
};

class ServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *) override
  {
    connected = true;
    Serial.println("PEER_CONNECTED");
  }
  void onDisconnect(BLEServer *server) override
  {
    connected = false;
    subscribed = false;
    Serial.println("PEER_DISCONNECTED");
    server->startAdvertising();
  }
};

CharacteristicCallbacks characteristicCallbacks;
DescriptorCallbacks descriptorCallbacks;
ServerCallbacks serverCallbacks;

void notifyPacket(const uint8_t *packet, size_t length, const char *tag)
{
  if (characteristic == nullptr || !subscribed)
  {
    Serial.println("PEER_NOT_SUBSCRIBED");
    return;
  }
  characteristic->setValue(const_cast<uint8_t *>(packet), length);
  characteristic->notify();
  Serial.printf("PEER_NOTIFIED %s length=%u\n", tag,
    static_cast<unsigned>(length));
}

void appendHeader(uint8_t *packet, size_t &length)
{
  const uint16_t now = timestamp();
  packet[length++] = static_cast<uint8_t>(0x80 | ((now >> 7) & 0x3F));
  packet[length++] = static_cast<uint8_t>(0x80 | (now & 0x7F));
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  BLEDevice::init(DEVICE_NAME);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(&serverCallbacks);
  BLEService *service = server->createService(SERVICE_UUID);
  characteristic = service->createCharacteristic(CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_WRITE_NR | BLECharacteristic::PROPERTY_NOTIFY);
  characteristic->setCallbacks(&characteristicCallbacks);
  BLE2902 *cccd = new BLE2902();
  cccd->setCallbacks(&descriptorCallbacks);
  characteristic->addDescriptor(cccd);
  service->start();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("MIDI_HOST_PEER_READY");
}

void loop()
{
  if (!Serial.available())
  {
    delay(1);
    return;
  }
  const int command = Serial.read();
  if (command == '?')
  {
    Serial.printf("PEER_STATE connected=%u subscribed=%u\n", connected ? 1 : 0,
      subscribed ? 1 : 0);
  }
  else if (command == 'n')
  {
    uint8_t packet[8];
    size_t length = 0;
    appendHeader(packet, length);
    packet[length++] = 0x90;
    packet[length++] = 0x3C;
    packet[length++] = 0x64;
    notifyPacket(packet, length, "note_on");
  }
  else if (command == 'r')
  {
    // Running status: the second Note On has no status byte of its own.
    uint8_t packet[8];
    size_t length = 0;
    appendHeader(packet, length);
    packet[length++] = 0x90;
    packet[length++] = 0x3C;
    packet[length++] = 0x64;
    packet[length++] = 0x3E;
    packet[length++] = 0x65;
    notifyPacket(packet, length, "running_status");
  }
  else if (command == 't')
  {
    // A System Real-Time byte (0xFE Active Sensing) between two Note Ons: it may
    // appear anywhere and must not disturb the running status around it.
    uint8_t packet[10];
    size_t length = 0;
    appendHeader(packet, length);
    packet[length++] = 0x90;
    packet[length++] = 0x3C;
    packet[length++] = 0x64;
    packet[length++] = static_cast<uint8_t>(0x80 | (timestamp() & 0x7F));
    packet[length++] = 0xFE;
    packet[length++] = 0x3E;
    packet[length++] = 0x65;
    notifyPacket(packet, length, "real_time");
  }
  else if (command == 'y')
  {
    // A SysEx across three notifications: 0x7D then a 40-byte ramp.
    uint8_t packet[32];
    size_t length = 0;
    appendHeader(packet, length);
    packet[length++] = 0xF0;
    packet[length++] = 0x7D;
    for (uint8_t value = 0; value < 10; ++value) packet[length++] = value;
    notifyPacket(packet, length, "sysex_first");
    delay(60);

    const uint16_t now = timestamp();
    length = 0;
    packet[length++] = static_cast<uint8_t>(0x80 | ((now >> 7) & 0x3F));
    for (uint8_t value = 10; value < 30; ++value) packet[length++] = value;
    notifyPacket(packet, length, "sysex_continuation");
    delay(60);

    length = 0;
    packet[length++] = static_cast<uint8_t>(0x80 | ((now >> 7) & 0x3F));
    for (uint8_t value = 30; value < 40; ++value) packet[length++] = value;
    packet[length++] = static_cast<uint8_t>(0x80 | (now & 0x7F));
    packet[length++] = 0xF7;
    notifyPacket(packet, length, "sysex_last");
  }
  else if (command == 'q')
  {
    Serial.printf("PEER_REPORT writes=%u last=%s\n", writeCount,
      lastWrite.length() > 0 ? lastWrite.c_str() : "none");
    Serial.printf(
      "PEER_WRITE_SYSEX_REPORT packets=%u bytes=%u start=%u end=%u ramp=%u\n",
      sysExPackets, static_cast<unsigned>(sysExBytes), sysExStarted ? 1 : 0,
      sysExEnded ? 1 : 0, sysExRamp ? 1 : 0);
  }
}
