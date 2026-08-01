#include "EspBleBluedroid.h"

#include <atomic>
#include <mutex>
#include <new>
#include <utility>

#if defined(CONFIG_BT_AVRCP_ENABLED)
#include <esp_avrc_api.h>
#endif

namespace
{
constexpr size_t AvrcpEventCapacity = 12;

String avrcpAddress(const uint8_t address[6])
{
  char value[18];
  snprintf(value, sizeof(value), "%02x:%02x:%02x:%02x:%02x:%02x",
    address[0], address[1], address[2], address[3], address[4], address[5]);
  return String(value);
}

template <typename Impl>
bool enqueueAvrcp(Impl *impl, const typename Impl::Event &event)
{
  std::lock_guard<std::mutex> lock(impl->mutex);
  if (impl->eventCount == AvrcpEventCapacity)
  {
    ++impl->droppedEvents;
    return false;
  }
  impl->events[(impl->eventHead + impl->eventCount) % AvrcpEventCapacity] =
    event;
  ++impl->eventCount;
  return true;
}
} // namespace

struct EspBluedroidAvrcpControllerImpl
{
  enum class EventType : uint8_t { Connected, Disconnected, Command, Volume };
  struct Event
  {
    EventType type = EventType::Connected;
    EspBluedroidAvrcpConnection connection;
    EspBluedroidAvrcpCommandEvent command;
    EspBluedroidAvrcpVolumeEvent volume;
  };
  mutable std::mutex mutex;
  bool ready = false;
  bool connected = false;
  String peerAddress;
  uint8_t nextLabel = 0;
  Event events[AvrcpEventCapacity];
  size_t eventHead = 0;
  size_t eventCount = 0;
  size_t droppedEvents = 0;
};

struct EspBluedroidAvrcpTargetImpl
{
  enum class EventType : uint8_t { Connected, Disconnected, Command, Volume };
  struct Event
  {
    EventType type = EventType::Connected;
    EspBluedroidAvrcpConnection connection;
    EspBluedroidAvrcpCommandEvent command;
    EspBluedroidAvrcpVolumeEvent volume;
  };
  mutable std::mutex mutex;
  bool ready = false;
  bool connected = false;
  bool volumeNotificationRegistered = false;
  String peerAddress;
  uint8_t volume = 64;
  Event events[AvrcpEventCapacity];
  size_t eventHead = 0;
  size_t eventCount = 0;
  size_t droppedEvents = 0;
};

#if defined(CONFIG_BT_AVRCP_ENABLED)
namespace
{
std::atomic<EspBluedroidAvrcpControllerImpl *> activeController{nullptr};
std::atomic<EspBluedroidAvrcpTargetImpl *> activeTarget{nullptr};
bool configureTarget();

uint8_t nextLabel(EspBluedroidAvrcpControllerImpl *impl)
{
  std::lock_guard<std::mutex> lock(impl->mutex);
  const uint8_t result = impl->nextLabel;
  impl->nextLabel = (impl->nextLabel + 1) & ESP_AVRC_TRANS_LABEL_MAX;
  return result;
}

void controllerCallback(
  esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *parameter)
{
  auto *impl = activeController.load();
  if (impl == nullptr || parameter == nullptr) return;

  EspBluedroidAvrcpControllerImpl::Event queued;
  if (event == ESP_AVRC_CT_CONNECTION_STATE_EVT)
  {
    queued.type = parameter->conn_stat.connected
      ? EspBluedroidAvrcpControllerImpl::EventType::Connected
      : EspBluedroidAvrcpControllerImpl::EventType::Disconnected;
    queued.connection.peerAddress = avrcpAddress(parameter->conn_stat.remote_bda);
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      impl->connected = parameter->conn_stat.connected;
      impl->peerAddress = parameter->conn_stat.connected
        ? queued.connection.peerAddress : String();
    }
    enqueueAvrcp(impl, queued);
  }
  else if (event == ESP_AVRC_CT_PASSTHROUGH_RSP_EVT)
  {
    queued.type = EspBluedroidAvrcpControllerImpl::EventType::Command;
    queued.command.command = static_cast<EspBluedroidAvrcpCommand>(
      parameter->psth_rsp.key_code);
    queued.command.state = static_cast<EspBluedroidAvrcpKeyState>(
      parameter->psth_rsp.key_state);
    queued.command.accepted =
      parameter->psth_rsp.rsp_code != ESP_AVRC_RSP_NOT_IMPL &&
      parameter->psth_rsp.rsp_code != ESP_AVRC_RSP_REJECT;
    enqueueAvrcp(impl, queued);
  }
  else if (event == ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT)
  {
    queued.type = EspBluedroidAvrcpControllerImpl::EventType::Volume;
    queued.volume.volume = parameter->set_volume_rsp.volume;
    enqueueAvrcp(impl, queued);
  }
  else if (event == ESP_AVRC_CT_CHANGE_NOTIFY_EVT &&
           parameter->change_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE)
  {
    queued.type = EspBluedroidAvrcpControllerImpl::EventType::Volume;
    queued.volume.volume = parameter->change_ntf.event_parameter.volume;
    enqueueAvrcp(impl, queued);
  }
}

void targetCallback(
  esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *parameter)
{
  auto *impl = activeTarget.load();
  if (impl == nullptr || parameter == nullptr) return;

  EspBluedroidAvrcpTargetImpl::Event queued;
  if (event == ESP_AVRC_TG_CONNECTION_STATE_EVT)
  {
    if (parameter->conn_stat.connected) configureTarget();
    queued.type = parameter->conn_stat.connected
      ? EspBluedroidAvrcpTargetImpl::EventType::Connected
      : EspBluedroidAvrcpTargetImpl::EventType::Disconnected;
    queued.connection.peerAddress = avrcpAddress(parameter->conn_stat.remote_bda);
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      impl->connected = parameter->conn_stat.connected;
      impl->peerAddress = parameter->conn_stat.connected
        ? queued.connection.peerAddress : String();
      if (!parameter->conn_stat.connected)
        impl->volumeNotificationRegistered = false;
    }
    enqueueAvrcp(impl, queued);
  }
  else if (event == ESP_AVRC_TG_PASSTHROUGH_CMD_EVT)
  {
    queued.type = EspBluedroidAvrcpTargetImpl::EventType::Command;
    queued.command.command = static_cast<EspBluedroidAvrcpCommand>(
      parameter->psth_cmd.key_code);
    queued.command.state = static_cast<EspBluedroidAvrcpKeyState>(
      parameter->psth_cmd.key_state);
    enqueueAvrcp(impl, queued);
  }
  else if (event == ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT)
  {
    queued.type = EspBluedroidAvrcpTargetImpl::EventType::Volume;
    queued.volume.volume = parameter->set_abs_vol.volume;
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      impl->volume = queued.volume.volume;
    }
    enqueueAvrcp(impl, queued);
  }
  else if (event == ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT &&
           parameter->reg_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE)
  {
    esp_avrc_rn_param_t response = {};
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      impl->volumeNotificationRegistered = true;
      response.volume = impl->volume;
    }
    esp_avrc_tg_send_rn_rsp(
      ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_INTERIM, &response);
  }
}

bool configureTarget()
{
  esp_avrc_psth_bit_mask_t allowed = {};
  if (esp_avrc_tg_get_psth_cmd_filter(
        ESP_AVRC_PSTH_FILTER_ALLOWED_CMD, &allowed) != ESP_OK)
    return false;
  esp_avrc_psth_bit_mask_t supported = {};
  const EspBluedroidAvrcpCommand commands[] = {
    EspBluedroidAvrcpCommand::Select, EspBluedroidAvrcpCommand::Up,
    EspBluedroidAvrcpCommand::Down, EspBluedroidAvrcpCommand::Left,
    EspBluedroidAvrcpCommand::Right, EspBluedroidAvrcpCommand::VolumeUp,
    EspBluedroidAvrcpCommand::VolumeDown, EspBluedroidAvrcpCommand::Mute,
    EspBluedroidAvrcpCommand::Play, EspBluedroidAvrcpCommand::Stop,
    EspBluedroidAvrcpCommand::Pause, EspBluedroidAvrcpCommand::Rewind,
    EspBluedroidAvrcpCommand::FastForward, EspBluedroidAvrcpCommand::Next,
    EspBluedroidAvrcpCommand::Previous,
  };
  for (auto command : commands)
  {
    const auto backend = static_cast<esp_avrc_pt_cmd_t>(command);
    if (esp_avrc_psth_bit_mask_operation(
          ESP_AVRC_BIT_MASK_OP_TEST, &allowed, backend))
      esp_avrc_psth_bit_mask_operation(
        ESP_AVRC_BIT_MASK_OP_SET, &supported, backend);
  }
  if (esp_avrc_tg_set_psth_cmd_filter(
        ESP_AVRC_PSTH_FILTER_SUPPORTED_CMD, &supported) != ESP_OK)
    return false;

  esp_avrc_rn_evt_cap_mask_t notifications = {};
  esp_avrc_rn_evt_bit_mask_operation(
    ESP_AVRC_BIT_MASK_OP_SET, &notifications, ESP_AVRC_RN_VOLUME_CHANGE);
  return esp_avrc_tg_set_rn_evt_cap(&notifications) == ESP_OK;
}
} // namespace
#endif

EspBluedroidAvrcpController::EspBluedroidAvrcpController(EspBleBluedroid *owner)
  : owner_(owner) {}
EspBluedroidAvrcpController::~EspBluedroidAvrcpController()
{ end(); delete impl_; }

bool EspBluedroidAvrcpController::start()
{
#if !defined(CONFIG_BT_AVRCP_ENABLED)
  owner_->setError(EspBleError::Unsupported,
    "CONFIG_BT_AVRCP_ENABLED is disabled by the Core build");
  return false;
#else
  if (!owner_->initialized())
  {
    owner_->setError(EspBleError::InvalidState,
      "initialize Bluetooth before starting AVRCP Controller");
    return false;
  }
  if (impl_ == nullptr)
    impl_ = new (std::nothrow) EspBluedroidAvrcpControllerImpl();
  if (impl_ == nullptr)
  {
    owner_->setError(EspBleError::ResourceExhausted,
      "failed to allocate AVRCP Controller state");
    return false;
  }
  if (started()) { owner_->clearError(); return true; }
  EspBluedroidAvrcpControllerImpl *expected = nullptr;
  if (!activeController.compare_exchange_strong(expected, impl_))
  {
    owner_->setError(EspBleError::BackendFailure,
      "another AVRCP Controller instance is active");
    return false;
  }
  if (esp_avrc_ct_init() != ESP_OK)
  {
    activeController.store(nullptr);
    owner_->setError(EspBleError::BackendFailure,
      "the Core rejected AVRCP Controller initialization");
    return false;
  }
  if (esp_avrc_ct_register_callback(controllerCallback) != ESP_OK)
  {
    esp_avrc_ct_deinit();
    activeController.store(nullptr);
    owner_->setError(EspBleError::BackendFailure,
      "the Core rejected the AVRCP Controller callback");
    return false;
  }
  { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->ready = true; }
  owner_->clearError();
  return true;
#endif
}

bool EspBluedroidAvrcpController::stop()
{ end(); owner_->clearError(); return true; }
bool EspBluedroidAvrcpController::started() const
{ if (!impl_) return false; std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->ready; }
bool EspBluedroidAvrcpController::connected() const
{ if (!impl_) return false; std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->connected; }
String EspBluedroidAvrcpController::peerAddress() const
{ if (!impl_) return ""; std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->peerAddress; }

bool EspBluedroidAvrcpController::sendCommand(
  EspBluedroidAvrcpCommand command, EspBluedroidAvrcpKeyState state)
{
#if defined(CONFIG_BT_AVRCP_ENABLED)
  if (!connected()) { owner_->setError(EspBleError::InvalidState, "AVRCP Controller is not connected"); return false; }
  if (esp_avrc_ct_send_passthrough_cmd(nextLabel(impl_),
        static_cast<uint8_t>(command), static_cast<uint8_t>(state)) != ESP_OK)
  { owner_->setError(EspBleError::BackendFailure, "the Core rejected the AVRCP command"); return false; }
  owner_->clearError(); return true;
#else
  (void)command; (void)state; owner_->setError(EspBleError::Unsupported, "CONFIG_BT_AVRCP_ENABLED is disabled by the Core build"); return false;
#endif
}

bool EspBluedroidAvrcpController::click(EspBluedroidAvrcpCommand command)
{
  return sendCommand(command, EspBluedroidAvrcpKeyState::Pressed) &&
         sendCommand(command, EspBluedroidAvrcpKeyState::Released);
}

bool EspBluedroidAvrcpController::setAbsoluteVolume(uint8_t volume)
{
#if defined(CONFIG_BT_AVRCP_ENABLED)
  if (volume > 127) { owner_->setError(EspBleError::InvalidArgument, "AVRCP absolute volume must be 0..127"); return false; }
  if (!connected()) { owner_->setError(EspBleError::InvalidState, "AVRCP Controller is not connected"); return false; }
  if (esp_avrc_ct_send_set_absolute_volume_cmd(nextLabel(impl_), volume) != ESP_OK)
  { owner_->setError(EspBleError::BackendFailure, "the Core rejected AVRCP absolute volume"); return false; }
  owner_->clearError(); return true;
#else
  (void)volume; owner_->setError(EspBleError::Unsupported, "CONFIG_BT_AVRCP_ENABLED is disabled by the Core build"); return false;
#endif
}

size_t EspBluedroidAvrcpController::droppedEventCount() const
{ if (!impl_) return 0; std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->droppedEvents; }
void EspBluedroidAvrcpController::onConnected(ConnectionCallback cb) { connectedCallback_ = std::move(cb); }
void EspBluedroidAvrcpController::onDisconnected(ConnectionCallback cb) { disconnectedCallback_ = std::move(cb); }
void EspBluedroidAvrcpController::onCommandResponse(CommandCallback cb) { commandCallback_ = std::move(cb); }
void EspBluedroidAvrcpController::onAbsoluteVolumeChanged(VolumeCallback cb) { volumeCallback_ = std::move(cb); }

void EspBluedroidAvrcpController::end()
{
#if defined(CONFIG_BT_AVRCP_ENABLED)
  if (started()) { activeController.store(nullptr); esp_avrc_ct_deinit(); }
#endif
  if (!impl_) return;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->ready = impl_->connected = false; impl_->peerAddress = "";
  impl_->eventHead = impl_->eventCount = 0;
}

void EspBluedroidAvrcpController::update()
{
  if (!impl_) return;
  while (true)
  {
    EspBluedroidAvrcpControllerImpl::Event event;
    { std::lock_guard<std::mutex> lock(impl_->mutex); if (!impl_->eventCount) break;
      event = impl_->events[impl_->eventHead]; impl_->eventHead = (impl_->eventHead + 1) % AvrcpEventCapacity; --impl_->eventCount; }
    if (event.type == EspBluedroidAvrcpControllerImpl::EventType::Connected && connectedCallback_) connectedCallback_(event.connection);
    else if (event.type == EspBluedroidAvrcpControllerImpl::EventType::Disconnected && disconnectedCallback_) disconnectedCallback_(event.connection);
    else if (event.type == EspBluedroidAvrcpControllerImpl::EventType::Command && commandCallback_) commandCallback_(event.command);
    else if (event.type == EspBluedroidAvrcpControllerImpl::EventType::Volume && volumeCallback_) volumeCallback_(event.volume);
  }
}

EspBluedroidAvrcpTarget::EspBluedroidAvrcpTarget(EspBleBluedroid *owner)
  : owner_(owner) {}
EspBluedroidAvrcpTarget::~EspBluedroidAvrcpTarget()
{ end(); delete impl_; }

bool EspBluedroidAvrcpTarget::start()
{
#if !defined(CONFIG_BT_AVRCP_ENABLED)
  owner_->setError(EspBleError::Unsupported, "CONFIG_BT_AVRCP_ENABLED is disabled by the Core build"); return false;
#else
  if (!owner_->initialized()) { owner_->setError(EspBleError::InvalidState, "initialize Bluetooth before starting AVRCP Target"); return false; }
  if (!impl_) impl_ = new (std::nothrow) EspBluedroidAvrcpTargetImpl();
  if (!impl_) { owner_->setError(EspBleError::ResourceExhausted, "failed to allocate AVRCP Target state"); return false; }
  if (started()) { owner_->clearError(); return true; }
  EspBluedroidAvrcpTargetImpl *expected = nullptr;
  if (!activeTarget.compare_exchange_strong(expected, impl_))
  { owner_->setError(EspBleError::BackendFailure, "another AVRCP Target instance is active"); return false; }
  if (esp_avrc_tg_init() != ESP_OK)
  { activeTarget.store(nullptr); owner_->setError(EspBleError::BackendFailure, "the Core rejected AVRCP Target initialization"); return false; }
  if (esp_avrc_tg_register_callback(targetCallback) != ESP_OK)
  { esp_avrc_tg_deinit(); activeTarget.store(nullptr); owner_->setError(EspBleError::BackendFailure, "the Core rejected the AVRCP Target callback"); return false; }
  { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->ready = true; }
  owner_->clearError(); return true;
#endif
}

bool EspBluedroidAvrcpTarget::stop()
{ end(); owner_->clearError(); return true; }
bool EspBluedroidAvrcpTarget::started() const
{ if (!impl_) return false; std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->ready; }
bool EspBluedroidAvrcpTarget::connected() const
{ if (!impl_) return false; std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->connected; }
String EspBluedroidAvrcpTarget::peerAddress() const
{ if (!impl_) return ""; std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->peerAddress; }

bool EspBluedroidAvrcpTarget::setAbsoluteVolume(uint8_t volume)
{
#if defined(CONFIG_BT_AVRCP_ENABLED)
  if (volume > 127) { owner_->setError(EspBleError::InvalidArgument, "AVRCP absolute volume must be 0..127"); return false; }
  if (!started()) { owner_->setError(EspBleError::InvalidState, "AVRCP Target is not started"); return false; }
  bool notify = false;
  { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->volume = volume; notify = impl_->volumeNotificationRegistered; impl_->volumeNotificationRegistered = false; }
  if (notify)
  { esp_avrc_rn_param_t response = {}; response.volume = volume;
    if (esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_CHANGED, &response) != ESP_OK)
    { owner_->setError(EspBleError::BackendFailure, "the Core rejected the AVRCP volume notification"); return false; } }
  owner_->clearError(); return true;
#else
  (void)volume; owner_->setError(EspBleError::Unsupported, "CONFIG_BT_AVRCP_ENABLED is disabled by the Core build"); return false;
#endif
}

uint8_t EspBluedroidAvrcpTarget::absoluteVolume() const
{ if (!impl_) return 0; std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->volume; }
size_t EspBluedroidAvrcpTarget::droppedEventCount() const
{ if (!impl_) return 0; std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->droppedEvents; }
void EspBluedroidAvrcpTarget::onConnected(ConnectionCallback cb) { connectedCallback_ = std::move(cb); }
void EspBluedroidAvrcpTarget::onDisconnected(ConnectionCallback cb) { disconnectedCallback_ = std::move(cb); }
void EspBluedroidAvrcpTarget::onCommand(CommandCallback cb) { commandCallback_ = std::move(cb); }
void EspBluedroidAvrcpTarget::onAbsoluteVolumeRequested(VolumeCallback cb) { volumeCallback_ = std::move(cb); }

void EspBluedroidAvrcpTarget::end()
{
#if defined(CONFIG_BT_AVRCP_ENABLED)
  if (started()) { activeTarget.store(nullptr); esp_avrc_tg_deinit(); }
#endif
  if (!impl_) return;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->ready = impl_->connected = impl_->volumeNotificationRegistered = false;
  impl_->peerAddress = ""; impl_->eventHead = impl_->eventCount = 0;
}

void EspBluedroidAvrcpTarget::update()
{
  if (!impl_) return;
  while (true)
  {
    EspBluedroidAvrcpTargetImpl::Event event;
    { std::lock_guard<std::mutex> lock(impl_->mutex); if (!impl_->eventCount) break;
      event = impl_->events[impl_->eventHead]; impl_->eventHead = (impl_->eventHead + 1) % AvrcpEventCapacity; --impl_->eventCount; }
    if (event.type == EspBluedroidAvrcpTargetImpl::EventType::Connected && connectedCallback_) connectedCallback_(event.connection);
    else if (event.type == EspBluedroidAvrcpTargetImpl::EventType::Disconnected && disconnectedCallback_) disconnectedCallback_(event.connection);
    else if (event.type == EspBluedroidAvrcpTargetImpl::EventType::Command && commandCallback_) commandCallback_(event.command);
    else if (event.type == EspBluedroidAvrcpTargetImpl::EventType::Volume && volumeCallback_) volumeCallback_(event.volume);
  }
}
