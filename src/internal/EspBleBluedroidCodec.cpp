#include "EspBleBluedroidCodec.h"

#include <cstdio>
#include <cstring>

namespace espblebluedroid
{
namespace internal
{
namespace
{
int hexValue(char value)
{
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

bool parseHexByte(const char *value, uint8_t &result)
{
  const int high = hexValue(value[0]);
  const int low = hexValue(value[1]);
  if (high < 0 || low < 0) return false;
  result = static_cast<uint8_t>((high << 4) | low);
  return true;
}

void expandUuid(const BleUuid &source, uint8_t destination[16])
{
  constexpr uint8_t BluetoothBaseUuid[] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  std::memcpy(destination, BluetoothBaseUuid, sizeof(BluetoothBaseUuid));
  if (source.bitSize == 128)
  {
    std::memcpy(destination, source.bytes.data(), 16);
    return;
  }
  const size_t byteCount = source.bitSize / 8;
  std::memcpy(destination + 12, source.bytes.data(), byteCount);
}
} // namespace

bool parseBleUuid(const char *value, BleUuid &uuid)
{
  if (value == nullptr) return false;
  const size_t length = std::strlen(value);
  BleUuid parsed;

  if (length == 4 || length == 8)
  {
    parsed.bitSize = static_cast<uint8_t>(length * 4);
    const size_t byteCount = length / 2;
    for (size_t index = 0; index < byteCount; ++index)
    {
      if (!parseHexByte(value + index * 2, parsed.bytes[byteCount - index - 1]))
      {
        return false;
      }
    }
  }
  else if (length == 36)
  {
    constexpr size_t Hyphens[] = {8, 13, 18, 23};
    for (const size_t position : Hyphens)
    {
      if (value[position] != '-') return false;
    }

    uint8_t canonical[16] = {};
    size_t canonicalIndex = 0;
    for (size_t index = 0; index < length;)
    {
      if (value[index] == '-')
      {
        ++index;
        continue;
      }
      if (canonicalIndex >= sizeof(canonical) ||
          !parseHexByte(value + index, canonical[canonicalIndex++]))
      {
        return false;
      }
      index += 2;
    }
    if (canonicalIndex != sizeof(canonical)) return false;
    parsed.bitSize = 128;
    for (size_t index = 0; index < sizeof(canonical); ++index)
    {
      parsed.bytes[index] = canonical[sizeof(canonical) - index - 1];
    }
  }
  else
  {
    return false;
  }

  uuid = parsed;
  return true;
}

bool uuidEquals(const char *left, const char *right)
{
  BleUuid leftUuid;
  BleUuid rightUuid;
  if (!parseBleUuid(left, leftUuid) || !parseBleUuid(right, rightUuid))
  {
    return false;
  }
  uint8_t expandedLeft[16] = {};
  uint8_t expandedRight[16] = {};
  expandUuid(leftUuid, expandedLeft);
  expandUuid(rightUuid, expandedRight);
  return std::memcmp(expandedLeft, expandedRight, sizeof(expandedLeft)) == 0;
}

bool parseBleAddress(const char *value, uint8_t address[6])
{
  if (value == nullptr || address == nullptr || std::strlen(value) != 17)
  {
    return false;
  }

  uint8_t parsed[6] = {};
  for (size_t index = 0; index < sizeof(parsed); ++index)
  {
    const size_t offset = index * 3;
    if (!parseHexByte(value + offset, parsed[index]))
    {
      return false;
    }
    if (index + 1 < sizeof(parsed) && value[offset + 2] != ':')
    {
      return false;
    }
  }
  std::memcpy(address, parsed, sizeof(parsed));
  return true;
}

std::string formatBleAddress(const uint8_t address[6])
{
  if (address == nullptr) return {};
  char value[18] = {};
  std::snprintf(
    value,
    sizeof(value),
    "%02x:%02x:%02x:%02x:%02x:%02x",
    address[0],
    address[1],
    address[2],
    address[3],
    address[4],
    address[5]);
  return value;
}

bool LegacyAdvertisingData::append(
  uint8_t type, const uint8_t *data, size_t length)
{
  if ((data == nullptr && length != 0) ||
      length > 0xfe ||
      size_ + length + 2 > Capacity)
  {
    return false;
  }

  data_[size_++] = static_cast<uint8_t>(length + 1);
  data_[size_++] = type;
  if (length != 0)
  {
    std::memcpy(data_.data() + size_, data, length);
    size_ += length;
  }
  return true;
}

const uint8_t *LegacyAdvertisingData::data() const
{
  return data_.data();
}

size_t LegacyAdvertisingData::size() const
{
  return size_;
}
} // namespace internal
} // namespace espblebluedroid
