#include "EspBleBluedroid.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <utility>

#if defined(CONFIG_BT_A2DP_ENABLE)
#include <esp_a2dp_api.h>
#include <esp_gap_bt_api.h>
#endif

namespace
{
constexpr size_t A2dpEventCapacity = 8;

bool parseAddress(const char *value, uint8_t address[6])
{
  if (value == nullptr) return false;

  unsigned int bytes[6] = {};
  char trailing = 0;
  if (sscanf(
        value, "%2x:%2x:%2x:%2x:%2x:%2x%c", &bytes[0], &bytes[1],
        &bytes[2], &bytes[3], &bytes[4], &bytes[5], &trailing) != 6)
  {
    return false;
  }

  for (size_t index = 0; index < 6; ++index)
  {
    if (bytes[index] > 0xff) return false;
    address[index] = static_cast<uint8_t>(bytes[index]);
  }
  return true;
}

String formatAddress(const uint8_t address[6])
{
  char value[18];
  snprintf(
    value, sizeof(value), "%02x:%02x:%02x:%02x:%02x:%02x",
    address[0], address[1], address[2], address[3], address[4], address[5]);
  return String(value);
}
} // namespace

struct EspBluedroidA2dpImpl
{
  enum class EventType : uint8_t
  {
    Started,
    Connected,
    ConnectionFailed,
    Disconnected,
    Stream,
  };

  struct Event
  {
    EventType type = EventType::Started;
    EspBluedroidA2dpStartResult start;
    EspBluedroidA2dpSession session;
    EspBluedroidA2dpConnectionFailure failure;
    EspBluedroidA2dpStreamChanged stream;
  };

  bool enqueue(const Event &event)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (eventCount == A2dpEventCapacity)
    {
      ++droppedEvents;
      return false;
    }
    events[(eventHead + eventCount) % A2dpEventCapacity] = event;
    ++eventCount;
    return true;
  }

  mutable std::mutex mutex;
  EspBleBluedroid *owner = nullptr;
  EspBluedroidA2dpRole role = EspBluedroidA2dpRole::Sink;
  bool initializing = false;
  bool ready = false;
  bool connecting = false;
  uint16_t backendHandle = 0;
  EspBluedroidA2dpCodecConfig negotiatedCodec;
  EspBluedroidA2dpSession activeSession;
  EspBluedroidA2dpSessionId nextSessionId = 1;
  Event events[A2dpEventCapacity];
  String connectingAddress;
  size_t eventHead = 0;
  size_t eventCount = 0;
  size_t droppedEvents = 0;
  EspBluedroidA2dpSink::PcmDataCallback pcmDataCallback;
  EspBluedroidA2dpSource::PcmRequestCallback pcmRequestCallback;
};

namespace
{
bool startA2dp(
  EspBluedroidA2dpImpl *&impl,
  EspBleBluedroid *owner,
  EspBluedroidA2dpRole role,
  const EspBluedroidA2dpSink::PcmDataCallback &pcmDataCallback,
  const EspBluedroidA2dpSource::PcmRequestCallback &pcmRequestCallback);
void stopA2dp(EspBluedroidA2dpImpl *impl);

EspBluedroidA2dpPcmFormat pcmFormat(
  const EspBluedroidA2dpCodecConfig &codec)
{
  EspBluedroidA2dpPcmFormat result;
  result.sampleRate = codec.sampleRate;
  result.channels = codec.channelCount;
  return result;
}
} // namespace

EspBluedroidA2dpSink::EspBluedroidA2dpSink(EspBleBluedroid *owner)
  : owner_(owner)
{
}

EspBluedroidA2dpSink::~EspBluedroidA2dpSink()
{
  end();
  delete impl_;
}

bool EspBluedroidA2dpSink::start()
{
#if !defined(CONFIG_BT_A2DP_ENABLE)
  owner_->setError(
    EspBleError::Unsupported,
    "CONFIG_BT_A2DP_ENABLE is disabled by the Core build");
  return false;
#else
  if (!owner_->initialized())
  {
    owner_->setError(
      EspBleError::InvalidState,
      "initialize Bluetooth before starting A2DP Sink");
    return false;
  }
  if (!startA2dp(
        impl_, owner_, EspBluedroidA2dpRole::Sink,
        pcmDataCallback_, nullptr))
  {
    owner_->setError(
      EspBleError::BackendFailure,
      "failed to initialize A2DP Sink or another A2DP role is active");
    return false;
  }
  owner_->clearError();
  return true;
#endif
}

bool EspBluedroidA2dpSink::stop()
{
  end();
  owner_->clearError();
  return true;
}

bool EspBluedroidA2dpSink::started() const
{
  if (impl_ == nullptr) return false;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->ready;
}

bool EspBluedroidA2dpSink::connect(const char *peerAddress)
{
#if !defined(CONFIG_BT_A2DP_ENABLE)
  (void)peerAddress;
  owner_->setError(
    EspBleError::Unsupported,
    "CONFIG_BT_A2DP_ENABLE is disabled by the Core build");
  return false;
#else
  if (!started())
  {
    owner_->setError(EspBleError::InvalidState, "A2DP Sink is not ready");
    return false;
  }

  uint8_t address[6];
  if (!parseAddress(peerAddress, address))
  {
    owner_->setError(
      EspBleError::InvalidArgument,
      "a canonical Classic peer address is required");
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->connecting || impl_->activeSession.id != 0)
    {
      owner_->setError(
        EspBleError::InvalidState,
        "A2DP Sink already has a connection or pending request");
      return false;
    }
    impl_->connecting = true;
    impl_->connectingAddress = peerAddress;
  }

  if (esp_a2d_sink_connect(address) != ESP_OK)
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->connecting = false;
    impl_->connectingAddress = "";
    owner_->setError(
      EspBleError::BackendFailure,
      "the Core rejected the A2DP Sink connection request");
    return false;
  }
  owner_->clearError();
  return true;
#endif
}

bool EspBluedroidA2dpSink::disconnect(EspBluedroidA2dpSessionId sessionId)
{
#if !defined(CONFIG_BT_A2DP_ENABLE)
  (void)sessionId;
  owner_->setError(
    EspBleError::Unsupported,
    "CONFIG_BT_A2DP_ENABLE is disabled by the Core build");
  return false;
#else
  if (impl_ == nullptr)
  {
    owner_->setError(EspBleError::NotFound, "A2DP Sink session was not found");
    return false;
  }

  uint8_t address[6];
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->activeSession.id != sessionId ||
        !parseAddress(impl_->activeSession.peerAddress.c_str(), address))
    {
      owner_->setError(
        EspBleError::NotFound, "A2DP Sink session was not found");
      return false;
    }
  }

  if (esp_a2d_sink_disconnect(address) != ESP_OK)
  {
    owner_->setError(
      EspBleError::BackendFailure,
      "the Core rejected the A2DP Sink disconnect request");
    return false;
  }
  owner_->clearError();
  return true;
#endif
}

bool EspBluedroidA2dpSink::session(EspBluedroidA2dpSession &session) const
{
  if (impl_ == nullptr) return false;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->activeSession.id == 0) return false;
  session = impl_->activeSession;
  return true;
}

size_t EspBluedroidA2dpSink::droppedEventCount() const
{
  if (impl_ == nullptr) return 0;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->droppedEvents;
}

void EspBluedroidA2dpSink::onConnected(SessionCallback callback)
{
  connectedCallback_ = std::move(callback);
}

void EspBluedroidA2dpSink::onDisconnected(SessionCallback callback)
{
  disconnectedCallback_ = std::move(callback);
}

void EspBluedroidA2dpSink::onStarted(StartCallback callback)
{
  startedCallback_ = std::move(callback);
}

void EspBluedroidA2dpSink::onConnectionFailed(
  ConnectionFailureCallback callback)
{
  connectionFailureCallback_ = std::move(callback);
}

void EspBluedroidA2dpSink::onStreamChanged(StreamCallback callback)
{
  streamCallback_ = std::move(callback);
}

void EspBluedroidA2dpSink::onPcmData(PcmDataCallback callback)
{
  pcmDataCallback_ = std::move(callback);
  if (impl_ != nullptr)
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->pcmDataCallback = pcmDataCallback_;
  }
}

void EspBluedroidA2dpSink::end()
{
  stopA2dp(impl_);
}

void EspBluedroidA2dpSink::update()
{
  if (impl_ == nullptr) return;
  while (true)
  {
    EspBluedroidA2dpImpl::Event event;
    {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      if (impl_->eventCount == 0) break;
      event = impl_->events[impl_->eventHead];
      impl_->eventHead = (impl_->eventHead + 1) % A2dpEventCapacity;
      --impl_->eventCount;
    }

    if (event.type == EspBluedroidA2dpImpl::EventType::Started &&
        startedCallback_)
      startedCallback_(event.start);
    else if (event.type == EspBluedroidA2dpImpl::EventType::Connected &&
             connectedCallback_)
      connectedCallback_(event.session);
    else if (
      event.type == EspBluedroidA2dpImpl::EventType::ConnectionFailed &&
      connectionFailureCallback_)
      connectionFailureCallback_(event.failure);
    else if (event.type == EspBluedroidA2dpImpl::EventType::Disconnected &&
             disconnectedCallback_)
      disconnectedCallback_(event.session);
    else if (event.type == EspBluedroidA2dpImpl::EventType::Stream &&
             streamCallback_)
      streamCallback_(event.stream);
  }
}

EspBluedroidA2dpSource::EspBluedroidA2dpSource(EspBleBluedroid *owner)
  : owner_(owner)
{
}

EspBluedroidA2dpSource::~EspBluedroidA2dpSource()
{
  end();
  delete impl_;
}

bool EspBluedroidA2dpSource::start()
{
#if !defined(CONFIG_BT_A2DP_ENABLE)
  owner_->setError(
    EspBleError::Unsupported,
    "CONFIG_BT_A2DP_ENABLE is disabled by the Core build");
  return false;
#else
  if (!owner_->initialized())
  {
    owner_->setError(
      EspBleError::InvalidState,
      "initialize Bluetooth before starting A2DP Source");
    return false;
  }
  if (!startA2dp(
        impl_, owner_, EspBluedroidA2dpRole::Source,
        nullptr, pcmRequestCallback_))
  {
    owner_->setError(
      EspBleError::BackendFailure,
      "failed to initialize A2DP Source or another A2DP role is active");
    return false;
  }
  owner_->clearError();
  return true;
#endif
}

bool EspBluedroidA2dpSource::stop()
{
  end();
  owner_->clearError();
  return true;
}

bool EspBluedroidA2dpSource::started() const
{
  if (impl_ == nullptr) return false;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->ready;
}

bool EspBluedroidA2dpSource::connect(const char *peerAddress)
{
#if !defined(CONFIG_BT_A2DP_ENABLE)
  (void)peerAddress;
  owner_->setError(
    EspBleError::Unsupported,
    "CONFIG_BT_A2DP_ENABLE is disabled by the Core build");
  return false;
#else
  if (!started())
  {
    owner_->setError(EspBleError::InvalidState, "A2DP Source is not ready");
    return false;
  }

  uint8_t address[6];
  if (!parseAddress(peerAddress, address))
  {
    owner_->setError(
      EspBleError::InvalidArgument,
      "a canonical Classic peer address is required");
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->connecting || impl_->activeSession.id != 0)
    {
      owner_->setError(
        EspBleError::InvalidState,
        "A2DP Source already has a connection or pending request");
      return false;
    }
    impl_->connecting = true;
    impl_->connectingAddress = peerAddress;
  }

  if (esp_a2d_source_connect(address) != ESP_OK)
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->connecting = false;
    impl_->connectingAddress = "";
    owner_->setError(
      EspBleError::BackendFailure,
      "the Core rejected the A2DP Source connection request");
    return false;
  }
  owner_->clearError();
  return true;
#endif
}

bool EspBluedroidA2dpSource::disconnect(EspBluedroidA2dpSessionId sessionId)
{
#if !defined(CONFIG_BT_A2DP_ENABLE)
  (void)sessionId;
  owner_->setError(
    EspBleError::Unsupported,
    "CONFIG_BT_A2DP_ENABLE is disabled by the Core build");
  return false;
#else
  if (impl_ == nullptr)
  {
    owner_->setError(
      EspBleError::NotFound, "A2DP Source session was not found");
    return false;
  }

  uint8_t address[6];
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->activeSession.id != sessionId ||
        !parseAddress(impl_->activeSession.peerAddress.c_str(), address))
    {
      owner_->setError(
        EspBleError::NotFound, "A2DP Source session was not found");
      return false;
    }
  }

  if (esp_a2d_source_disconnect(address) != ESP_OK)
  {
    owner_->setError(
      EspBleError::BackendFailure,
      "the Core rejected the A2DP Source disconnect request");
    return false;
  }
  owner_->clearError();
  return true;
#endif
}

bool EspBluedroidA2dpSource::session(EspBluedroidA2dpSession &session) const
{
  if (impl_ == nullptr) return false;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->activeSession.id == 0) return false;
  session = impl_->activeSession;
  return true;
}

size_t EspBluedroidA2dpSource::droppedEventCount() const
{
  if (impl_ == nullptr) return 0;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->droppedEvents;
}

bool EspBluedroidA2dpSource::startStream()
{
#if !defined(CONFIG_BT_A2DP_ENABLE)
  owner_->setError(
    EspBleError::Unsupported,
    "CONFIG_BT_A2DP_ENABLE is disabled by the Core build");
  return false;
#else
  EspBluedroidA2dpSession current;
  if (!session(current))
  {
    owner_->setError(
      EspBleError::InvalidState, "A2DP Source is not connected");
    return false;
  }
  if (esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START) != ESP_OK)
  {
    owner_->setError(
      EspBleError::BackendFailure,
      "the Core rejected the A2DP start command");
    return false;
  }
  owner_->clearError();
  return true;
#endif
}

bool EspBluedroidA2dpSource::suspendStream()
{
#if !defined(CONFIG_BT_A2DP_ENABLE)
  owner_->setError(
    EspBleError::Unsupported,
    "CONFIG_BT_A2DP_ENABLE is disabled by the Core build");
  return false;
#else
  EspBluedroidA2dpSession current;
  if (!session(current))
  {
    owner_->setError(
      EspBleError::InvalidState, "A2DP Source is not connected");
    return false;
  }
  if (esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND) != ESP_OK)
  {
    owner_->setError(
      EspBleError::BackendFailure,
      "the Core rejected the A2DP suspend command");
    return false;
  }
  owner_->clearError();
  return true;
#endif
}

void EspBluedroidA2dpSource::onPcmRequested(PcmRequestCallback callback)
{
  pcmRequestCallback_ = std::move(callback);
  if (impl_ != nullptr)
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->pcmRequestCallback = pcmRequestCallback_;
  }
}

void EspBluedroidA2dpSource::onConnected(SessionCallback callback)
{
  connectedCallback_ = std::move(callback);
}

void EspBluedroidA2dpSource::onDisconnected(SessionCallback callback)
{
  disconnectedCallback_ = std::move(callback);
}

void EspBluedroidA2dpSource::onStarted(StartCallback callback)
{
  startedCallback_ = std::move(callback);
}

void EspBluedroidA2dpSource::onConnectionFailed(
  ConnectionFailureCallback callback)
{
  connectionFailureCallback_ = std::move(callback);
}

void EspBluedroidA2dpSource::onStreamChanged(StreamCallback callback)
{
  streamCallback_ = std::move(callback);
}

void EspBluedroidA2dpSource::end()
{
  stopA2dp(impl_);
}

void EspBluedroidA2dpSource::update()
{
  if (impl_ == nullptr) return;
  while (true)
  {
    EspBluedroidA2dpImpl::Event event;
    {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      if (impl_->eventCount == 0) break;
      event = impl_->events[impl_->eventHead];
      impl_->eventHead = (impl_->eventHead + 1) % A2dpEventCapacity;
      --impl_->eventCount;
    }

    if (event.type == EspBluedroidA2dpImpl::EventType::Started &&
        startedCallback_)
      startedCallback_(event.start);
    else if (event.type == EspBluedroidA2dpImpl::EventType::Connected &&
             connectedCallback_)
      connectedCallback_(event.session);
    else if (
      event.type == EspBluedroidA2dpImpl::EventType::ConnectionFailed &&
      connectionFailureCallback_)
      connectionFailureCallback_(event.failure);
    else if (event.type == EspBluedroidA2dpImpl::EventType::Disconnected &&
             disconnectedCallback_)
      disconnectedCallback_(event.session);
    else if (event.type == EspBluedroidA2dpImpl::EventType::Stream &&
             streamCallback_)
      streamCallback_(event.stream);
  }
}

#if defined(CONFIG_BT_A2DP_ENABLE)
namespace
{
std::atomic<EspBluedroidA2dpImpl *> activeA2dp{nullptr};

EspBluedroidA2dpCodecConfig codecConfig(const esp_a2d_mcc_t &mediaCodec)
{
  EspBluedroidA2dpCodecConfig result;
  if (mediaCodec.type != ESP_A2D_MCT_SBC) return result;

  result.codec = EspBluedroidA2dpCodec::Sbc;
  const esp_a2d_cie_sbc_t &sbc = mediaCodec.cie.sbc_info;
  result.sampleRate =
    (sbc.samp_freq & ESP_A2D_SBC_CIE_SF_48K) ? 48000 :
    (sbc.samp_freq & ESP_A2D_SBC_CIE_SF_44K) ? 44100 :
    (sbc.samp_freq & ESP_A2D_SBC_CIE_SF_32K) ? 32000 :
    (sbc.samp_freq & ESP_A2D_SBC_CIE_SF_16K) ? 16000 : 0;
  result.channelMode = sbc.ch_mode;
  result.channelCount =
    (sbc.ch_mode & ESP_A2D_SBC_CIE_CH_MODE_MONO) ? 1 : 2;
  result.blockLength =
    (sbc.block_len & ESP_A2D_SBC_CIE_BLOCK_LEN_16) ? 16 :
    (sbc.block_len & ESP_A2D_SBC_CIE_BLOCK_LEN_12) ? 12 :
    (sbc.block_len & ESP_A2D_SBC_CIE_BLOCK_LEN_8) ? 8 : 4;
  result.subbands =
    (sbc.num_subbands & ESP_A2D_SBC_CIE_NUM_SUBBANDS_8) ? 8 : 4;
  result.minBitpool = sbc.min_bitpool;
  result.maxBitpool = sbc.max_bitpool;
  return result;
}

esp_a2d_mcc_t defaultSbcEndpoint()
{
  esp_a2d_mcc_t result = {};
  result.type = ESP_A2D_MCT_SBC;
  result.cie.sbc_info.samp_freq =
    ESP_A2D_SBC_CIE_SF_16K | ESP_A2D_SBC_CIE_SF_32K |
    ESP_A2D_SBC_CIE_SF_44K | ESP_A2D_SBC_CIE_SF_48K;
  result.cie.sbc_info.ch_mode =
    ESP_A2D_SBC_CIE_CH_MODE_MONO |
    ESP_A2D_SBC_CIE_CH_MODE_DUAL_CHANNEL |
    ESP_A2D_SBC_CIE_CH_MODE_STEREO |
    ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO;
  result.cie.sbc_info.block_len =
    ESP_A2D_SBC_CIE_BLOCK_LEN_4 | ESP_A2D_SBC_CIE_BLOCK_LEN_8 |
    ESP_A2D_SBC_CIE_BLOCK_LEN_12 | ESP_A2D_SBC_CIE_BLOCK_LEN_16;
  result.cie.sbc_info.num_subbands =
    ESP_A2D_SBC_CIE_NUM_SUBBANDS_4 | ESP_A2D_SBC_CIE_NUM_SUBBANDS_8;
  result.cie.sbc_info.alloc_mthd =
    ESP_A2D_SBC_CIE_ALLOC_MTHD_SNR |
    ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS;
  result.cie.sbc_info.min_bitpool = 2;
  result.cie.sbc_info.max_bitpool = 53;
  return result;
}

void a2dpCallback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *parameter)
{
  EspBluedroidA2dpImpl *impl = activeA2dp.load();
  if (impl == nullptr || parameter == nullptr) return;

  if (event == ESP_A2D_CONNECTION_STATE_EVT)
  {
    if (parameter->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED)
    {
      EspBluedroidA2dpImpl::Event queued;
      queued.type = EspBluedroidA2dpImpl::EventType::Connected;
      {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->activeSession.id = impl->nextSessionId++;
        if (impl->nextSessionId == 0) impl->nextSessionId = 1;
        impl->activeSession.peerAddress =
          formatAddress(parameter->conn_stat.remote_bda);
        impl->activeSession.role = impl->role;
        impl->activeSession.incoming = !impl->connecting;
        impl->activeSession.audioMtu = parameter->conn_stat.audio_mtu;
        impl->activeSession.codec = impl->negotiatedCodec;
        impl->backendHandle = parameter->conn_stat.conn_hdl;
        impl->connecting = false;
        impl->connectingAddress = "";
        queued.session = impl->activeSession;
      }
      impl->enqueue(queued);
    }
    else if (
      parameter->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
    {
      EspBluedroidA2dpImpl::Event queued;
      {
        std::lock_guard<std::mutex> lock(impl->mutex);
        if (impl->activeSession.id == 0 && impl->connecting)
        {
          queued.type = EspBluedroidA2dpImpl::EventType::ConnectionFailed;
          queued.failure.peerAddress = impl->connectingAddress;
          queued.failure.role = impl->role;
          queued.failure.detail =
            "the A2DP connection was rejected or failed";
        }
        else
        {
          queued.type = EspBluedroidA2dpImpl::EventType::Disconnected;
          queued.session = impl->activeSession;
        }
        impl->activeSession = EspBluedroidA2dpSession();
        impl->negotiatedCodec = EspBluedroidA2dpCodecConfig();
        impl->backendHandle = 0;
        impl->connecting = false;
        impl->connectingAddress = "";
      }
      if (queued.type == EspBluedroidA2dpImpl::EventType::ConnectionFailed ||
          queued.session.id != 0)
      {
        impl->enqueue(queued);
      }
    }
  }
  else if (event == ESP_A2D_AUDIO_CFG_EVT)
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->negotiatedCodec = codecConfig(parameter->audio_cfg.mcc);
    if (impl->activeSession.id != 0)
      impl->activeSession.codec = impl->negotiatedCodec;
  }
  else if (event == ESP_A2D_AUDIO_STATE_EVT)
  {
    EspBluedroidA2dpImpl::Event queued;
    queued.type = EspBluedroidA2dpImpl::EventType::Stream;
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      queued.stream.sessionId = impl->activeSession.id;
      queued.stream.state =
        parameter->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED
        ? EspBluedroidA2dpStreamState::Started
        : EspBluedroidA2dpStreamState::Suspended;
      impl->activeSession.streaming =
        queued.stream.state == EspBluedroidA2dpStreamState::Started;
    }
    if (queued.stream.sessionId != 0) impl->enqueue(queued);
  }
}

void a2dpSinkPcmCallback(const uint8_t *data, uint32_t length)
{
  EspBluedroidA2dpImpl *impl = activeA2dp.load();
  if (impl == nullptr || data == nullptr || length == 0) return;

  EspBluedroidA2dpSink::PcmDataCallback callback;
  EspBluedroidA2dpPcmData pcm;
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    callback = impl->pcmDataCallback;
    pcm.sessionId = impl->activeSession.id;
    pcm.format = pcmFormat(impl->activeSession.codec);
  }
  if (!callback || pcm.sessionId == 0 ||
      pcm.format.sampleRate == 0 || pcm.format.channels == 0)
  {
    return;
  }

  pcm.data = data;
  pcm.length = length;
  callback(pcm);
}

int32_t a2dpSourcePcmCallback(uint8_t *data, int32_t length)
{
  EspBluedroidA2dpImpl *impl = activeA2dp.load();
  if (impl == nullptr) return 0;

  EspBluedroidA2dpSource::PcmRequestCallback callback;
  EspBluedroidA2dpPcmRequest request;
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    callback = impl->pcmRequestCallback;
    request.sessionId = impl->activeSession.id;
    request.format = pcmFormat(impl->activeSession.codec);
  }
  request.flush = length < 0;
  request.data = request.flush ? nullptr : data;
  request.capacity = request.flush ? 0 : static_cast<size_t>(length);
  if (!callback) return 0;
  if (request.flush)
  {
    callback(request);
    return 0;
  }
  if (request.sessionId == 0 || request.format.sampleRate == 0 ||
      request.format.channels == 0)
  {
    return 0;
  }
  callback(request);
  if (request.written > request.capacity) return 0;
  return static_cast<int32_t>(request.written);
}
} // namespace
#endif

namespace
{
bool startA2dp(
  EspBluedroidA2dpImpl *&impl,
  EspBleBluedroid *owner,
  EspBluedroidA2dpRole role,
  const EspBluedroidA2dpSink::PcmDataCallback &pcmDataCallback,
  const EspBluedroidA2dpSource::PcmRequestCallback &pcmRequestCallback)
{
#if !defined(CONFIG_BT_A2DP_ENABLE)
  (void)impl;
  (void)owner;
  (void)role;
  (void)pcmDataCallback;
  (void)pcmRequestCallback;
  return false;
#else
  if (!owner->initialized()) return false;
  if (impl == nullptr) impl = new (std::nothrow) EspBluedroidA2dpImpl();
  if (impl == nullptr) return false;

  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->pcmDataCallback = pcmDataCallback;
    impl->pcmRequestCallback = pcmRequestCallback;
    if (impl->ready || impl->initializing) return true;
    impl->owner = owner;
    impl->role = role;
    impl->initializing = true;
  }

  EspBluedroidA2dpImpl *expected = nullptr;
  if (!activeA2dp.compare_exchange_strong(expected, impl))
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->initializing = false;
    return false;
  }

  esp_err_t status = esp_a2d_register_callback(a2dpCallback);
  if (status == ESP_OK)
  {
    status = role == EspBluedroidA2dpRole::Sink
      ? esp_a2d_sink_register_data_callback(a2dpSinkPcmCallback)
      : esp_a2d_source_register_data_callback(a2dpSourcePcmCallback);
  }
  if (status == ESP_OK)
  {
    status = role == EspBluedroidA2dpRole::Sink
      ? esp_a2d_sink_init()
      : esp_a2d_source_init();
  }
  if (status != ESP_OK)
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->initializing = false;
    activeA2dp.store(nullptr);
    return false;
  }

  const esp_a2d_mcc_t endpoint = defaultSbcEndpoint();
  status = role == EspBluedroidA2dpRole::Sink
    ? esp_a2d_sink_register_stream_endpoint(0, &endpoint)
    : esp_a2d_source_register_stream_endpoint(0, &endpoint);
  if (status != ESP_OK)
  {
    if (role == EspBluedroidA2dpRole::Sink) esp_a2d_sink_deinit();
    else esp_a2d_source_deinit();
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->initializing = false;
    activeA2dp.store(nullptr);
    return false;
  }

  if (role == EspBluedroidA2dpRole::Sink &&
      esp_bt_gap_set_scan_mode(
        ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE) != ESP_OK)
  {
    esp_a2d_sink_deinit();
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->initializing = false;
    activeA2dp.store(nullptr);
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->initializing = false;
    impl->ready = true;
  }
  EspBluedroidA2dpImpl::Event queued;
  queued.type = EspBluedroidA2dpImpl::EventType::Started;
  queued.start.role = role;
  queued.start.success = true;
  impl->enqueue(queued);
  return true;
#endif
}

void stopA2dp(EspBluedroidA2dpImpl *impl)
{
  if (impl == nullptr) return;

#if defined(CONFIG_BT_A2DP_ENABLE)
  bool active = false;
  EspBluedroidA2dpRole role = EspBluedroidA2dpRole::Sink;
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    active = impl->ready || impl->initializing;
    role = impl->role;
  }
  if (active)
  {
    activeA2dp.store(nullptr);
    if (role == EspBluedroidA2dpRole::Sink) esp_a2d_sink_deinit();
    else esp_a2d_source_deinit();
  }
#endif

  std::lock_guard<std::mutex> lock(impl->mutex);
  impl->initializing = false;
  impl->ready = false;
  impl->connecting = false;
  impl->backendHandle = 0;
  impl->connectingAddress = "";
  impl->activeSession = EspBluedroidA2dpSession();
  impl->negotiatedCodec = EspBluedroidA2dpCodecConfig();
  impl->eventHead = 0;
  impl->eventCount = 0;
  impl->pcmDataCallback = nullptr;
  impl->pcmRequestCallback = nullptr;
}
} // namespace
