#include "EspBleBluedroidGattcState.h"

namespace espblebluedroid
{
namespace internal
{
bool GattcState::beginRegistration()
{
  if (state_ != GattcLinkState::Unregistered) return false;
  state_ = GattcLinkState::Registering;
  return true;
}

bool GattcState::registered(uint8_t gattcIf)
{
  if (state_ != GattcLinkState::Registering ||
      gattcIf == InvalidGattcIf)
  {
    return false;
  }
  gattcIf_ = gattcIf;
  state_ = GattcLinkState::Idle;
  return true;
}

uint32_t GattcState::beginOpen()
{
  if (state_ != GattcLinkState::Idle) return 0;
  attempt_ = nextAttempt_++;
  if (nextAttempt_ == 0) nextAttempt_ = 1;
  if (attempt_ == 0)
  {
    attempt_ = nextAttempt_++;
    if (nextAttempt_ == 0) nextAttempt_ = 1;
  }
  state_ = GattcLinkState::Opening;
  return attempt_;
}

bool GattcState::beginCancel(uint32_t attempt)
{
  if (state_ != GattcLinkState::Opening || !matchesAttempt(attempt))
  {
    return false;
  }
  state_ = GattcLinkState::Cancelling;
  return true;
}

GattcOpenResult GattcState::opened(
  uint32_t attempt, uint16_t connectionId)
{
  if (!matchesAttempt(attempt) ||
      connectionId == InvalidConnectionId)
  {
    return GattcOpenResult::Ignored;
  }
  if (state_ == GattcLinkState::Opening)
  {
    connectionId_ = connectionId;
    state_ = GattcLinkState::Connected;
    return GattcOpenResult::Connected;
  }
  if (state_ == GattcLinkState::Cancelling)
  {
    connectionId_ = connectionId;
    state_ = GattcLinkState::Closing;
    return GattcOpenResult::ConnectedAfterCancel;
  }
  return GattcOpenResult::Ignored;
}

bool GattcState::openFailed(uint32_t attempt)
{
  if (!matchesAttempt(attempt) ||
      (state_ != GattcLinkState::Opening &&
       state_ != GattcLinkState::Cancelling))
  {
    return false;
  }
  connectionId_ = InvalidConnectionId;
  state_ = GattcLinkState::Idle;
  return true;
}

bool GattcState::cancelled(uint32_t attempt)
{
  if (state_ != GattcLinkState::Cancelling ||
      !matchesAttempt(attempt))
  {
    return false;
  }
  connectionId_ = InvalidConnectionId;
  state_ = GattcLinkState::Idle;
  return true;
}

bool GattcState::beginClose(uint16_t connectionId)
{
  if (state_ != GattcLinkState::Connected ||
      connectionId_ != connectionId)
  {
    return false;
  }
  state_ = GattcLinkState::Closing;
  return true;
}

bool GattcState::disconnected(uint16_t connectionId)
{
  if ((state_ != GattcLinkState::Connected &&
       state_ != GattcLinkState::Closing) ||
      connectionId_ != connectionId)
  {
    return false;
  }
  connectionId_ = InvalidConnectionId;
  state_ = GattcLinkState::Idle;
  return true;
}

void GattcState::reset()
{
  state_ = GattcLinkState::Unregistered;
  gattcIf_ = InvalidGattcIf;
  connectionId_ = InvalidConnectionId;
  attempt_ = 0;
  ++nextAttempt_;
  if (nextAttempt_ == 0) nextAttempt_ = 1;
}

GattcLinkState GattcState::linkState() const
{
  return state_;
}

uint8_t GattcState::gattcIf() const
{
  return gattcIf_;
}

uint16_t GattcState::connectionId() const
{
  return connectionId_;
}

uint32_t GattcState::attempt() const
{
  return attempt_;
}

bool GattcState::matchesAttempt(uint32_t attempt) const
{
  return attempt != 0 && attempt_ == attempt;
}
} // namespace internal
} // namespace espblebluedroid
