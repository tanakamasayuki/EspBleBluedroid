#pragma once

#include <cstdint>

namespace espblebluedroid
{
namespace internal
{
enum class GattcLinkState : uint8_t
{
  Unregistered,
  Registering,
  Idle,
  Opening,
  Cancelling,
  Connected,
  Closing,
};

enum class GattcOpenResult : uint8_t
{
  Ignored,
  Connected,
  ConnectedAfterCancel,
};

class GattcState
{
public:
  static constexpr uint8_t InvalidGattcIf = 0xff;
  static constexpr uint16_t InvalidConnectionId = 0xffff;

  bool beginRegistration();
  bool registered(uint8_t gattcIf);
  uint32_t beginOpen();
  bool beginCancel(uint32_t attempt);
  GattcOpenResult opened(uint32_t attempt, uint16_t connectionId);
  bool openFailed(uint32_t attempt);
  bool cancelled(uint32_t attempt);
  bool beginClose(uint16_t connectionId);
  bool disconnected(uint16_t connectionId);
  void reset();

  GattcLinkState linkState() const;
  uint8_t gattcIf() const;
  uint16_t connectionId() const;
  uint32_t attempt() const;

private:
  bool matchesAttempt(uint32_t attempt) const;

  GattcLinkState state_ = GattcLinkState::Unregistered;
  uint8_t gattcIf_ = InvalidGattcIf;
  uint16_t connectionId_ = InvalidConnectionId;
  uint32_t nextAttempt_ = 1;
  uint32_t attempt_ = 0;
};
} // namespace internal
} // namespace espblebluedroid
