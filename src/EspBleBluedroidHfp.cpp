#include "EspBleBluedroid.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <utility>

#if defined(CONFIG_BT_CLASSIC_ENABLED)
#include <esp_gap_bt_api.h>
#endif

#if defined(CONFIG_BT_HFP_CLIENT_ENABLE)
#include <esp_hf_client_api.h>
#include <esp_hf_client_legacy_api.h>
#endif
#if defined(CONFIG_BT_HFP_AG_ENABLE)
#include <esp_hf_ag_api.h>
#include <esp_hf_ag_legacy_api.h>
#endif

namespace
{
constexpr size_t HfpEventCapacity = 8;

bool parseHfpAddress(const char *value, uint8_t address[6])
{
  if (value == nullptr) return false;
  unsigned int bytes[6] = {};
  char trailing = 0;
  if (sscanf(
        value, "%2x:%2x:%2x:%2x:%2x:%2x%c", &bytes[0], &bytes[1],
        &bytes[2], &bytes[3], &bytes[4], &bytes[5], &trailing) != 6)
    return false;
  for (size_t index = 0; index < 6; ++index)
  {
    if (bytes[index] > 0xff) return false;
    address[index] = static_cast<uint8_t>(bytes[index]);
  }
  return true;
}

String hfpAddress(const uint8_t address[6])
{
  char value[18];
  snprintf(
    value, sizeof(value), "%02x:%02x:%02x:%02x:%02x:%02x",
    address[0], address[1], address[2], address[3], address[4], address[5]);
  return String(value);
}

EspBluedroidHfpPcmFormat hfpFormat(EspBluedroidHfpCodec codec)
{
  EspBluedroidHfpPcmFormat format;
  format.sampleRate = codec == EspBluedroidHfpCodec::Msbc ? 16000 :
    codec == EspBluedroidHfpCodec::Cvsd ? 8000 : 0;
  return format;
}
} // namespace

struct EspBluedroidHfpImpl
{
  enum class EventType : uint8_t
  {
    Started,
    Connected,
    ConnectionFailed,
    Disconnected,
    Audio,
  };

  struct Event
  {
    EventType type = EventType::Started;
    EspBluedroidHfpStartResult start;
    EspBluedroidHfpSession session;
    EspBluedroidHfpConnectionFailure failure;
    EspBluedroidHfpAudioChanged audio;
  };

  bool enqueue(const Event &event)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (eventCount == HfpEventCapacity)
    {
      ++droppedEvents;
      return false;
    }
    events[(eventHead + eventCount) % HfpEventCapacity] = event;
    ++eventCount;
    return true;
  }

  void setError(EspBleError error, const char *detail)
  {
    owner->setError(error, detail);
  }

  mutable std::mutex mutex;
  EspBleBluedroid *owner = nullptr;
  EspBluedroidHfpRole role = EspBluedroidHfpRole::HandsFree;
  bool initializing = false;
  bool ready = false;
  bool connecting = false;
  String connectingAddress;
  EspBluedroidHfpSession activeSession;
  EspBluedroidHfpSessionId nextSessionId = 1;
  Event events[HfpEventCapacity];
  size_t eventHead = 0;
  size_t eventCount = 0;
  size_t droppedEvents = 0;
  EspBluedroidHfpHandsFree::PcmDataCallback pcmDataCallback;
  EspBluedroidHfpHandsFree::PcmRequestCallback pcmRequestCallback;
};

namespace
{
std::atomic<EspBluedroidHfpImpl *> activeHandsFree{nullptr};
std::atomic<EspBluedroidHfpImpl *> activeAudioGateway{nullptr};

void enqueueStarted(EspBluedroidHfpImpl *impl, bool success)
{
  if (impl == nullptr) return;
#if defined(CONFIG_BT_CLASSIC_ENABLED)
  if (success && impl->role == EspBluedroidHfpRole::AudioGateway &&
      esp_bt_gap_set_scan_mode(
        ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE) != ESP_OK)
    success = false;
#endif
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->initializing = false;
    impl->ready = success;
  }
  EspBluedroidHfpImpl::Event event;
  event.type = EspBluedroidHfpImpl::EventType::Started;
  event.start.role = impl->role;
  event.start.success = success;
  event.start.error = success ? EspBleError::None : EspBleError::BackendFailure;
  if (!success) event.start.detail = "the Core failed to initialize HFP";
  impl->enqueue(event);
}

void enqueueConnection(
  EspBluedroidHfpImpl *impl, int state, const uint8_t address[6],
  int slcConnectedState)
{
  if (impl == nullptr) return;
  const String peer = hfpAddress(address);
  if (state == slcConnectedState)
  {
    EspBluedroidHfpImpl::Event event;
    event.type = EspBluedroidHfpImpl::EventType::Connected;
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      const bool incoming = !impl->connecting;
      impl->activeSession = EspBluedroidHfpSession();
      impl->activeSession.id = impl->nextSessionId++;
      impl->activeSession.peerAddress = peer;
      impl->activeSession.role = impl->role;
      impl->activeSession.incoming = incoming;
      impl->connecting = false;
      impl->connectingAddress = "";
      event.session = impl->activeSession;
    }
    impl->enqueue(event);
  }
  else if (state == 0)
  {
    EspBluedroidHfpImpl::Event event;
    bool connectionFailed = false;
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      connectionFailed = impl->connecting && impl->activeSession.id == 0;
      if (connectionFailed)
      {
        event.type = EspBluedroidHfpImpl::EventType::ConnectionFailed;
        event.failure.peerAddress = impl->connectingAddress;
        event.failure.role = impl->role;
        event.failure.detail = "the HFP service-level connection failed";
      }
      else
      {
        event.type = EspBluedroidHfpImpl::EventType::Disconnected;
        event.session = impl->activeSession;
        if (event.session.peerAddress.length() == 0)
          event.session.peerAddress = peer;
      }
      impl->connecting = false;
      impl->connectingAddress = "";
      impl->activeSession = EspBluedroidHfpSession();
    }
    if (connectionFailed || event.session.id != 0) impl->enqueue(event);
  }
}

void enqueueAudio(EspBluedroidHfpImpl *impl, int state, int msbcState)
{
  if (impl == nullptr) return;
  if (state != 0 && state != 2 && state != msbcState) return;
  EspBluedroidHfpImpl::Event event;
  event.type = EspBluedroidHfpImpl::EventType::Audio;
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (impl->activeSession.id == 0) return;
    const bool connected = state == 2 || state == msbcState;
    const EspBluedroidHfpCodec codec = !connected
      ? EspBluedroidHfpCodec::Unknown
      : state == msbcState ? EspBluedroidHfpCodec::Msbc
                           : EspBluedroidHfpCodec::Cvsd;
    impl->activeSession.audioConnected = connected;
    impl->activeSession.codec = codec;
    impl->activeSession.format = hfpFormat(codec);
    event.audio.sessionId = impl->activeSession.id;
    event.audio.connected = connected;
    event.audio.codec = codec;
    event.audio.format = impl->activeSession.format;
  }
  impl->enqueue(event);
}

void receivePcm(EspBluedroidHfpImpl *impl, const uint8_t *data, uint32_t length)
{
  if (impl == nullptr || data == nullptr || length == 0) return;
  EspBluedroidHfpHandsFree::PcmDataCallback callback;
  EspBluedroidHfpPcmData pcm;
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    callback = impl->pcmDataCallback;
    pcm.sessionId = impl->activeSession.id;
    pcm.format = impl->activeSession.format;
  }
  if (callback && pcm.sessionId != 0)
  {
    pcm.data = data;
    pcm.length = length;
    callback(pcm);
  }
}

uint32_t requestPcm(EspBluedroidHfpImpl *impl, uint8_t *data, uint32_t length)
{
  if (impl == nullptr || data == nullptr || length == 0) return 0;
  EspBluedroidHfpHandsFree::PcmRequestCallback callback;
  EspBluedroidHfpPcmRequest request;
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    callback = impl->pcmRequestCallback;
    request.sessionId = impl->activeSession.id;
    request.format = impl->activeSession.format;
  }
  if (!callback || request.sessionId == 0) return 0;
  request.data = data;
  request.capacity = length;
  callback(request);
  return static_cast<uint32_t>(request.written > length ? length : request.written);
}

#if defined(CONFIG_BT_HFP_CLIENT_ENABLE)
void handsFreePcmIn(const uint8_t *data, uint32_t length)
{
  receivePcm(activeHandsFree.load(std::memory_order_acquire), data, length);
}

uint32_t handsFreePcmOut(uint8_t *data, uint32_t length)
{
  return requestPcm(
    activeHandsFree.load(std::memory_order_acquire), data, length);
}

void handsFreeCallback(
  esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *parameter)
{
  EspBluedroidHfpImpl *impl =
    activeHandsFree.load(std::memory_order_acquire);
  if (impl == nullptr || parameter == nullptr) return;
  if (event == ESP_HF_CLIENT_PROF_STATE_EVT)
    enqueueStarted(impl, parameter->prof_stat.state == ESP_HF_INIT_SUCCESS);
  else if (event == ESP_HF_CLIENT_CONNECTION_STATE_EVT)
    enqueueConnection(
      impl, parameter->conn_stat.state, parameter->conn_stat.remote_bda,
      ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED);
  else if (event == ESP_HF_CLIENT_AUDIO_STATE_EVT)
    enqueueAudio(
      impl, parameter->audio_stat.state,
      ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC);
}
#endif

#if defined(CONFIG_BT_HFP_AG_ENABLE)
void audioGatewayPcmIn(const uint8_t *data, uint32_t length)
{
  receivePcm(activeAudioGateway.load(std::memory_order_acquire), data, length);
}

uint32_t audioGatewayPcmOut(uint8_t *data, uint32_t length)
{
  return requestPcm(
    activeAudioGateway.load(std::memory_order_acquire), data, length);
}

void audioGatewayCallback(esp_hf_cb_event_t event, esp_hf_cb_param_t *parameter)
{
  EspBluedroidHfpImpl *impl =
    activeAudioGateway.load(std::memory_order_acquire);
  if (impl == nullptr || parameter == nullptr) return;
  if (event == ESP_HF_PROF_STATE_EVT)
    enqueueStarted(impl, parameter->prof_stat.state == ESP_HF_INIT_SUCCESS);
  else if (event == ESP_HF_CONNECTION_STATE_EVT)
    enqueueConnection(
      impl, parameter->conn_stat.state, parameter->conn_stat.remote_bda,
      ESP_HF_CONNECTION_STATE_SLC_CONNECTED);
  else if (event == ESP_HF_AUDIO_STATE_EVT)
    enqueueAudio(
      impl, parameter->audio_stat.state, ESP_HF_AUDIO_STATE_CONNECTED_MSBC);
  else if (event == ESP_HF_CIND_RESPONSE_EVT)
    esp_hf_ag_cind_response(
      parameter->cind_rep.remote_addr, ESP_HF_CALL_STATUS_NO_CALLS,
      ESP_HF_CALL_SETUP_STATUS_IDLE, ESP_HF_NETWORK_STATE_AVAILABLE, 5,
      ESP_HF_ROAMING_STATUS_INACTIVE, 5, ESP_HF_CALL_HELD_STATUS_NONE);
  else if (event == ESP_HF_COPS_RESPONSE_EVT)
  {
    char name[] = "EspBleBluedroid";
    esp_hf_ag_cops_response(parameter->cops_rep.remote_addr, name);
  }
}
#endif

bool startHfp(
  EspBluedroidHfpImpl *&impl, EspBleBluedroid *owner,
  EspBluedroidHfpRole role,
  const EspBluedroidHfpHandsFree::PcmDataCallback &dataCallback,
  const EspBluedroidHfpHandsFree::PcmRequestCallback &requestCallback)
{
  if (impl == nullptr) impl = new (std::nothrow) EspBluedroidHfpImpl();
  if (impl == nullptr) return false;
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (impl->ready || impl->initializing) return true;
    impl->owner = owner;
    impl->role = role;
    impl->initializing = true;
    impl->pcmDataCallback = dataCallback;
    impl->pcmRequestCallback = requestCallback;
  }

  esp_err_t result = ESP_FAIL;
  if (role == EspBluedroidHfpRole::HandsFree)
  {
#if defined(CONFIG_BT_HFP_CLIENT_ENABLE)
    EspBluedroidHfpImpl *expected = nullptr;
    if (!activeHandsFree.compare_exchange_strong(expected, impl)) goto failed;
    if (esp_hf_client_register_callback(handsFreeCallback) != ESP_OK ||
        esp_hf_client_register_data_callback(
          handsFreePcmIn, handsFreePcmOut) != ESP_OK)
      goto failed;
    result = esp_hf_client_init();
#endif
  }
  else
  {
#if defined(CONFIG_BT_HFP_AG_ENABLE)
    EspBluedroidHfpImpl *expected = nullptr;
    if (!activeAudioGateway.compare_exchange_strong(expected, impl)) goto failed;
    if (esp_hf_ag_register_callback(audioGatewayCallback) != ESP_OK ||
        esp_hf_ag_register_data_callback(
          audioGatewayPcmIn, audioGatewayPcmOut) != ESP_OK)
      goto failed;
    result = esp_hf_ag_init();
#endif
  }
  if (result == ESP_OK) return true;

failed:
  if (role == EspBluedroidHfpRole::HandsFree)
  {
    EspBluedroidHfpImpl *expected = impl;
    activeHandsFree.compare_exchange_strong(expected, nullptr);
  }
  else
  {
    EspBluedroidHfpImpl *expected = impl;
    activeAudioGateway.compare_exchange_strong(expected, nullptr);
  }
  std::lock_guard<std::mutex> lock(impl->mutex);
  impl->initializing = false;
  return false;
}

bool hfpCoreAvailable(EspBluedroidHfpRole role)
{
  if (role == EspBluedroidHfpRole::HandsFree)
  {
#if defined(CONFIG_BT_HFP_CLIENT_ENABLE)
    return true;
#else
    return false;
#endif
  }
#if defined(CONFIG_BT_HFP_AG_ENABLE)
  return true;
#else
  return false;
#endif
}

void stopHfp(EspBluedroidHfpImpl *impl)
{
  if (impl == nullptr) return;
  EspBluedroidHfpRole role;
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    role = impl->role;
  }
  if (role == EspBluedroidHfpRole::HandsFree)
  {
    EspBluedroidHfpImpl *expected = impl;
    if (activeHandsFree.compare_exchange_strong(expected, nullptr))
    {
#if defined(CONFIG_BT_HFP_CLIENT_ENABLE)
      esp_hf_client_deinit();
#endif
    }
  }
  else
  {
    EspBluedroidHfpImpl *expected = impl;
    if (activeAudioGateway.compare_exchange_strong(expected, nullptr))
    {
#if defined(CONFIG_BT_HFP_AG_ENABLE)
      esp_hf_ag_deinit();
#endif
    }
  }
  std::lock_guard<std::mutex> lock(impl->mutex);
  impl->initializing = false;
  impl->ready = false;
  impl->connecting = false;
  impl->connectingAddress = "";
  impl->activeSession = EspBluedroidHfpSession();
  impl->eventHead = 0;
  impl->eventCount = 0;
}

bool hfpConnect(
  EspBleBluedroid *owner, EspBluedroidHfpImpl *impl,
  const char *peerAddress)
{
  bool ready = false;
  if (impl != nullptr)
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    ready = impl->ready;
  }
  if (!ready)
  {
    impl->setError(EspBleError::InvalidState, "HFP is not ready");
    return false;
  }
  uint8_t address[6];
  if (!parseHfpAddress(peerAddress, address))
  {
    impl->setError(
      EspBleError::InvalidArgument,
      "a canonical Classic peer address is required");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (impl->connecting || impl->activeSession.id != 0)
    {
      impl->setError(
        EspBleError::InvalidState, "HFP already has a connection");
      return false;
    }
    impl->connecting = true;
    impl->connectingAddress = peerAddress;
  }
  esp_err_t result = ESP_FAIL;
  if (impl->role == EspBluedroidHfpRole::HandsFree)
  {
#if defined(CONFIG_BT_HFP_CLIENT_ENABLE)
    result = esp_hf_client_connect(address);
#endif
  }
  else
  {
#if defined(CONFIG_BT_HFP_AG_ENABLE)
    result = esp_hf_ag_slc_connect(address);
#endif
  }
  if (result != ESP_OK)
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->connecting = false;
    impl->connectingAddress = "";
    impl->setError(
      EspBleError::BackendFailure, "the Core rejected the HFP connection");
    return false;
  }
  owner->clearError();
  return true;
}

bool hfpSessionAction(
  EspBleBluedroid *owner, EspBluedroidHfpImpl *impl,
  EspBluedroidHfpSessionId id, int action)
{
  uint8_t address[6];
  EspBluedroidHfpRole role;
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (impl->activeSession.id != id || id == 0 ||
        !parseHfpAddress(
          impl->activeSession.peerAddress.c_str(), address))
    {
      impl->setError(EspBleError::NotFound, "HFP session not found");
      return false;
    }
    role = impl->role;
  }
  esp_err_t result = ESP_FAIL;
  if (role == EspBluedroidHfpRole::HandsFree)
  {
#if defined(CONFIG_BT_HFP_CLIENT_ENABLE)
    result = action == 0 ? esp_hf_client_disconnect(address) :
      action == 1 ? esp_hf_client_connect_audio(address) :
                    esp_hf_client_disconnect_audio(address);
#endif
  }
  else
  {
#if defined(CONFIG_BT_HFP_AG_ENABLE)
    result = action == 0 ? esp_hf_ag_slc_disconnect(address) :
      action == 1 ? esp_hf_ag_audio_connect(address) :
                    esp_hf_ag_audio_disconnect(address);
#endif
  }
  if (result != ESP_OK)
  {
    impl->setError(
      EspBleError::BackendFailure, "the Core rejected the HFP request");
    return false;
  }
  owner->clearError();
  return true;
}

void updateHfp(
  EspBluedroidHfpImpl *impl,
  const EspBluedroidHfpHandsFree::StartCallback &startedCallback,
  const EspBluedroidHfpHandsFree::SessionCallback &connectedCallback,
  const EspBluedroidHfpHandsFree::SessionCallback &disconnectedCallback,
  const EspBluedroidHfpHandsFree::ConnectionFailureCallback &failureCallback,
  const EspBluedroidHfpHandsFree::AudioCallback &audioCallback)
{
  if (impl == nullptr) return;
  bool audioConnected = false;
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    audioConnected = impl->activeSession.audioConnected;
  }
  if (audioConnected)
  {
    if (impl->role == EspBluedroidHfpRole::HandsFree)
    {
#if defined(CONFIG_BT_HFP_CLIENT_ENABLE)
      esp_hf_client_outgoing_data_ready();
#endif
    }
    else
    {
#if defined(CONFIG_BT_HFP_AG_ENABLE)
      esp_hf_ag_outgoing_data_ready();
#endif
    }
  }
  while (true)
  {
    EspBluedroidHfpImpl::Event event;
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      if (impl->eventCount == 0) break;
      event = std::move(impl->events[impl->eventHead]);
      impl->eventHead = (impl->eventHead + 1) % HfpEventCapacity;
      --impl->eventCount;
    }
    if (event.type == EspBluedroidHfpImpl::EventType::Started &&
        startedCallback)
      startedCallback(event.start);
    else if (event.type == EspBluedroidHfpImpl::EventType::Connected &&
             connectedCallback)
      connectedCallback(event.session);
    else if (event.type == EspBluedroidHfpImpl::EventType::Disconnected &&
             disconnectedCallback)
      disconnectedCallback(event.session);
    else if (event.type == EspBluedroidHfpImpl::EventType::ConnectionFailed &&
             failureCallback)
      failureCallback(event.failure);
    else if (event.type == EspBluedroidHfpImpl::EventType::Audio &&
             audioCallback)
      audioCallback(event.audio);
  }
}
} // namespace

#define DEFINE_HFP_PROFILE(ClassName, RoleValue, ConfigMacro)                    \
ClassName::ClassName(EspBleBluedroid *owner) : owner_(owner) {}                 \
ClassName::~ClassName() { end(); delete impl_; }                                \
bool ClassName::start()                                                         \
{                                                                               \
  if (!hfpCoreAvailable(RoleValue))                                             \
  {                                                                             \
    owner_->setError(EspBleError::Unsupported,                                  \
      "the HFP role is disabled by the Core build");                            \
    return false;                                                               \
  }                                                                             \
  if (!owner_->initialized())                                                   \
  {                                                                             \
    owner_->setError(EspBleError::InvalidState,                                 \
      "initialize Bluetooth before starting HFP");                             \
    return false;                                                               \
  }                                                                             \
  if (!startHfp(impl_, owner_, RoleValue, pcmDataCallback_, pcmRequestCallback_))\
  {                                                                             \
    owner_->setError(EspBleError::BackendFailure, "failed to initialize HFP"); \
    return false;                                                               \
  }                                                                             \
  owner_->clearError();                                                         \
  return true;                                                                  \
}                                                                               \
bool ClassName::stop() { end(); owner_->clearError(); return true; }             \
bool ClassName::started() const                                                 \
{                                                                               \
  if (impl_ == nullptr) return false;                                            \
  std::lock_guard<std::mutex> lock(impl_->mutex);                               \
  return impl_->ready;                                                          \
}                                                                               \
bool ClassName::connect(const char *address)                                      \
{                                                                                 \
  if (impl_ == nullptr) { owner_->setError(EspBleError::InvalidState, "HFP is not started"); return false; }\
  return hfpConnect(owner_, impl_, address);                                      \
}                                                                                 \
bool ClassName::disconnect(EspBluedroidHfpSessionId id)                         \
{ if (!impl_) { owner_->setError(EspBleError::InvalidState, "HFP is not started"); return false; } return hfpSessionAction(owner_, impl_, id, 0); }\
bool ClassName::connectAudio(EspBluedroidHfpSessionId id)                       \
{ if (!impl_) { owner_->setError(EspBleError::InvalidState, "HFP is not started"); return false; } return hfpSessionAction(owner_, impl_, id, 1); }\
bool ClassName::disconnectAudio(EspBluedroidHfpSessionId id)                    \
{ if (!impl_) { owner_->setError(EspBleError::InvalidState, "HFP is not started"); return false; } return hfpSessionAction(owner_, impl_, id, 2); }\
bool ClassName::session(EspBluedroidHfpSession &session) const                  \
{                                                                               \
  if (impl_ == nullptr) return false;                                            \
  std::lock_guard<std::mutex> lock(impl_->mutex);                               \
  if (impl_->activeSession.id == 0) return false;                               \
  session = impl_->activeSession;                                                \
  return true;                                                                  \
}                                                                               \
size_t ClassName::droppedEventCount() const                                     \
{                                                                               \
  if (impl_ == nullptr) return 0;                                                \
  std::lock_guard<std::mutex> lock(impl_->mutex);                               \
  return impl_->droppedEvents;                                                   \
}                                                                               \
void ClassName::onStarted(StartCallback cb) { startedCallback_ = std::move(cb); }\
void ClassName::onConnected(SessionCallback cb) { connectedCallback_ = std::move(cb); }\
void ClassName::onDisconnected(SessionCallback cb) { disconnectedCallback_ = std::move(cb); }\
void ClassName::onConnectionFailed(ConnectionFailureCallback cb) { connectionFailureCallback_ = std::move(cb); }\
void ClassName::onAudioChanged(AudioCallback cb) { audioCallback_ = std::move(cb); }\
void ClassName::onPcmData(PcmDataCallback cb)                                   \
{                                                                               \
  pcmDataCallback_ = std::move(cb);                                              \
  if (impl_) { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->pcmDataCallback = pcmDataCallback_; }\
}                                                                               \
void ClassName::onPcmRequested(PcmRequestCallback cb)                           \
{                                                                               \
  pcmRequestCallback_ = std::move(cb);                                           \
  if (impl_) { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->pcmRequestCallback = pcmRequestCallback_; }\
}                                                                               \
void ClassName::end() { stopHfp(impl_); }                                        \
void ClassName::update()                                                        \
{ updateHfp(impl_, startedCallback_, connectedCallback_, disconnectedCallback_, \
    connectionFailureCallback_, audioCallback_); }

DEFINE_HFP_PROFILE(
  EspBluedroidHfpHandsFree, EspBluedroidHfpRole::HandsFree,
  CONFIG_BT_HFP_CLIENT_ENABLE)
DEFINE_HFP_PROFILE(
  EspBluedroidHfpAudioGateway, EspBluedroidHfpRole::AudioGateway,
  CONFIG_BT_HFP_AG_ENABLE)

#undef DEFINE_HFP_PROFILE
