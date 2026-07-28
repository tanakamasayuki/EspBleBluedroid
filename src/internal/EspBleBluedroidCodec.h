#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace espblebluedroid
{
namespace internal
{
struct BleUuid
{
  uint8_t bitSize = 0;
  std::array<uint8_t, 16> bytes = {};
};

bool parseBleUuid(const char *value, BleUuid &uuid);
bool uuidEquals(const char *left, const char *right);

bool parseBleAddress(const char *value, uint8_t address[6]);
std::string formatBleAddress(const uint8_t address[6]);

class LegacyAdvertisingData
{
public:
  static constexpr size_t Capacity = 31;

  bool append(
    uint8_t type, const uint8_t *data, size_t length);
  const uint8_t *data() const;
  size_t size() const;

private:
  std::array<uint8_t, Capacity> data_ = {};
  size_t size_ = 0;
};
} // namespace internal
} // namespace espblebluedroid
