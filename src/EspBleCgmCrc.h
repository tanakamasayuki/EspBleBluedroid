#ifndef ESP_BLE_CGM_CRC_H
#define ESP_BLE_CGM_CRC_H

// Backend-independent E2E-CRC codec for the Continuous Glucose Monitoring (CGM)
// Service. The CGM Service specification defines its End-to-End CRC as CRC-CCITT
// with polynomial 0x1021 and initial value 0xffff, with data fed in
// least-significant-bit first (reflected). That is the CRC-16/MCRF4XX variant
// (reflected polynomial 0x8408, no final XOR), whose documented check value for
// the ASCII string "123456789" is 0x6f91.
//
// The CRC is appended little-endian as the final two octets of every CGM
// characteristic that carries E2E-CRC protection (Measurement, Feature, Status,
// Session Start/Run Time, and the Specific Ops Control Point), and is computed
// over all preceding octets of the characteristic value.
//
// The header is shared with EspBle so both libraries produce the same wire
// bytes, and is Arduino-independent so it can be verified with host unit tests
// (tests/unit/cgm_crc_test.cpp).

#include <stddef.h>
#include <stdint.h>

// Compute the CGM E2E-CRC over length octets of data.
inline uint16_t espBleCgmCrc(const uint8_t *data, size_t length)
{
  uint16_t crc = 0xffff;
  for (size_t index = 0; index < length; ++index)
  {
    crc ^= static_cast<uint16_t>(data[index]);
    for (int bit = 0; bit < 8; ++bit)
    {
      if (crc & 0x0001)
      {
        crc = static_cast<uint16_t>((crc >> 1) ^ 0x8408); // reflected 0x1021
      }
      else
      {
        crc = static_cast<uint16_t>(crc >> 1);
      }
    }
  }
  return crc;
}

// Append the little-endian E2E-CRC over the first length octets of buffer at
// buffer[length] and buffer[length + 1]. The buffer must hold length + 2 octets.
// Returns the total length including the CRC.
inline size_t espBleCgmAppendCrc(uint8_t *buffer, size_t length)
{
  const uint16_t crc = espBleCgmCrc(buffer, length);
  buffer[length] = static_cast<uint8_t>(crc & 0xff);
  buffer[length + 1] = static_cast<uint8_t>((crc >> 8) & 0xff);
  return length + 2;
}

// Verify a CGM characteristic value whose final two octets are a little-endian
// E2E-CRC over the preceding octets. Returns false when length < 2 or the CRC
// does not match.
inline bool espBleCgmVerifyCrc(const uint8_t *data, size_t length)
{
  if (length < 2) return false;
  const size_t payload = length - 2;
  const uint16_t expected = espBleCgmCrc(data, payload);
  const uint16_t actual = static_cast<uint16_t>(data[payload]) |
    (static_cast<uint16_t>(data[payload + 1]) << 8);
  return expected == actual;
}

#endif // ESP_BLE_CGM_CRC_H
