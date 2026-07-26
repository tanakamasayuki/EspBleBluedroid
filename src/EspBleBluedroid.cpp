#include "EspBleBluedroid.h"

#include <BLEAdvertising.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLERemoteCharacteristic.h>
#include <BLERemoteDescriptor.h>
#include <BLERemoteService.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <atomic>
#include <cctype>
#include <cstring>
#include <mutex>
#include <new>
#include <utility>

#if defined(CONFIG_BT_CLASSIC_ENABLED)
#include <esp_gap_bt_api.h>
#include <esp32-hal-alloc-bt-classic-mem.h>
#endif
#if defined(CONFIG_BT_SPP_ENABLED)
#include <esp_spp_api.h>
#endif

namespace
{
constexpr size_t ScanQueueCapacity = 16;
constexpr size_t ClassicInquiryQueueCapacity = 16;
constexpr size_t SppEventQueueCapacity = 8;
constexpr size_t LegacyAdvertisingPayloadCapacity = 31;

bool appendAdvertisingData(
  BLEAdvertisementData &payload, uint8_t type, const String &data)
{
  const size_t previousLength = payload.getPayload().length();
  const size_t fieldLength = data.length() + 2;
  if (data.length() > 0xfe ||
      previousLength + fieldLength > LegacyAdvertisingPayloadCapacity)
  {
    return false;
  }

  const char header[2] = {
    static_cast<char>(data.length() + 1), static_cast<char>(type)};
  payload.addData(String(header, sizeof(header)) + data);
  return payload.getPayload().length() == previousLength + fieldLength;
}

bool uuidEquals(const String &left, const char *right)
{
  if (right == nullptr || right[0] == '\0' || left.isEmpty())
  {
    return false;
  }
  if (left.equalsIgnoreCase(right))
  {
    return true;
  }
  return BLEUUID(left.c_str()).equals(BLEUUID(right));
}

BLERemoteCharacteristic *findCharacteristicByHandle(
  BLEClient *client, uint16_t handle, String &serviceUuid)
{
  if (client == nullptr || handle == 0) return nullptr;
  std::map<std::string, BLERemoteService *> *services = client->getServices();
  if (services == nullptr) return nullptr;
  for (const auto &serviceItem : *services)
  {
    BLERemoteService *service = serviceItem.second;
    if (service == nullptr) continue;
    std::map<uint16_t, BLERemoteCharacteristic *> *characteristics =
      service->getCharacteristicsByHandle();
    if (characteristics == nullptr) continue;
    const auto found = characteristics->find(handle);
    if (found != characteristics->end() && found->second != nullptr)
    {
      serviceUuid = service->getUUID().toString();
      return found->second;
    }
  }
  return nullptr;
}

bool sameSecurityConfig(
  const EspBleSecurityConfig &left, const EspBleSecurityConfig &right)
{
  return left.enabled == right.enabled &&
    left.bonding == right.bonding &&
    left.pairOnConnect == right.pairOnConnect &&
    left.mitm == right.mitm &&
    left.ioCapability == right.ioCapability &&
    left.staticPasskeyEnabled == right.staticPasskeyEnabled &&
    left.staticPasskey == right.staticPasskey;
}

bool isValidBleAddress(const char *address)
{
  if (address == nullptr || strlen(address) != 17)
  {
    return false;
  }
  for (size_t index = 0; index < 17; ++index)
  {
    if ((index + 1) % 3 == 0)
    {
      if (address[index] != ':') return false;
    }
    else if (!std::isxdigit(static_cast<unsigned char>(address[index])))
    {
      return false;
    }
  }
  return true;
}
} // namespace

struct EspBleScannerImpl
{
  class BackendCallbacks : public BLEAdvertisedDeviceCallbacks
  {
  public:
    explicit BackendCallbacks(EspBleScannerImpl *owner) : owner_(owner) {}

    void onResult(BLEAdvertisedDevice device) override
    {
      EspBleScanResult result;
      result.address = device.getAddress().toString();
      result.addressType = static_cast<EspBleAddressType>(device.getAddressType());
      result.rssi = device.getRSSI();
      result.connectable = device.isConnectable();
      result.scannable = device.isScannable();

      if (device.haveName())
      {
        result.name = device.getName();
      }
      if (device.haveManufacturerData())
      {
        result.manufacturerData = device.getManufacturerData();
      }

      const int serviceCount = device.getServiceUUIDCount();
      for (int index = 0;
           index < serviceCount &&
             result.serviceUuidCount < EspBleScanResult::MaxServiceUuids;
           ++index)
      {
        result.serviceUuids[result.serviceUuidCount++] =
          device.getServiceUUID(index).toString();
      }

      owner_->enqueue(std::move(result));
    }

  private:
    EspBleScannerImpl *owner_;
  };

  EspBleScannerImpl() : callbacks(this) {}

  bool enqueue(EspBleScanResult result)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (count == ScanQueueCapacity)
    {
      ++dropped;
      return false;
    }
    const size_t tail = (head + count) % ScanQueueCapacity;
    queue[tail] = std::move(result);
    ++count;
    return true;
  }

  mutable std::mutex mutex;
  EspBleScanResult queue[ScanQueueCapacity];
  size_t head = 0;
  size_t count = 0;
  size_t dropped = 0;
  BackendCallbacks callbacks;
};

struct EspBluedroidClassicInquiryImpl
{
  bool enqueue(EspBluedroidClassicInquiryResult result)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (count == ClassicInquiryQueueCapacity)
    {
      ++dropped;
      return false;
    }
    const size_t tail = (head + count) % ClassicInquiryQueueCapacity;
    queue[tail] = std::move(result);
    ++count;
    return true;
  }

  mutable std::mutex mutex;
  EspBluedroidClassicInquiryResult queue[ClassicInquiryQueueCapacity];
  size_t head = 0;
  size_t count = 0;
  size_t dropped = 0;
  bool running = false;
  bool stopRequested = false;
  bool completionPending = false;
  bool completionCancelled = false;
};

#if defined(CONFIG_BT_CLASSIC_ENABLED)
namespace
{
std::atomic<EspBluedroidClassicInquiryImpl *> activeClassicInquiry{nullptr};

String classicAddress(const esp_bd_addr_t address)
{
  char value[18];
  snprintf(
    value, sizeof(value), "%02x:%02x:%02x:%02x:%02x:%02x",
    address[0], address[1], address[2], address[3], address[4], address[5]);
  return String(value);
}

void classicGapCallback(
  esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *parameter)
{
  EspBluedroidClassicInquiryImpl *impl =
    activeClassicInquiry.load(std::memory_order_acquire);
  if (impl == nullptr || parameter == nullptr) return;

  if (event == ESP_BT_GAP_DISC_RES_EVT)
  {
    EspBluedroidClassicInquiryResult result;
    result.address = classicAddress(parameter->disc_res.bda);
    uint8_t *eir = nullptr;
    for (int index = 0; index < parameter->disc_res.num_prop; ++index)
    {
      const esp_bt_gap_dev_prop_t &property =
        parameter->disc_res.prop[index];
      if (property.val == nullptr) continue;
      if (property.type == ESP_BT_GAP_DEV_PROP_BDNAME)
      {
        const size_t length =
          property.len > 0 && static_cast<const char *>(property.val)
              [property.len - 1] == '\0'
          ? property.len - 1
          : property.len;
        result.name = String(
          static_cast<const char *>(property.val), length);
      }
      else if (
        property.type == ESP_BT_GAP_DEV_PROP_COD &&
        property.len >= sizeof(uint32_t))
      {
        memcpy(
          &result.classOfDevice, property.val,
          sizeof(result.classOfDevice));
        result.hasClassOfDevice = true;
      }
      else if (
        property.type == ESP_BT_GAP_DEV_PROP_RSSI &&
        property.len >= sizeof(int8_t))
      {
        int8_t rssi = 0;
        memcpy(&rssi, property.val, sizeof(rssi));
        result.rssi = rssi;
        result.hasRssi = true;
      }
      else if (property.type == ESP_BT_GAP_DEV_PROP_EIR)
      {
        eir = static_cast<uint8_t *>(property.val);
      }
    }
    if (result.name.isEmpty() && eir != nullptr)
    {
      uint8_t length = 0;
      uint8_t *name = esp_bt_gap_resolve_eir_data(
        eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &length);
      if (name == nullptr)
      {
        name = esp_bt_gap_resolve_eir_data(
          eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &length);
      }
      if (name != nullptr && length > 0)
      {
        result.name = String(
          reinterpret_cast<const char *>(name), length);
      }
    }
    impl->enqueue(std::move(result));
  }
  else if (
    event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT &&
    parameter->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED)
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->running = false;
    impl->completionCancelled = impl->stopRequested;
    impl->stopRequested = false;
    impl->completionPending = true;
  }
}
} // namespace
#endif

struct EspBluedroidSppImpl
{
  enum class EventType : uint8_t
  {
    ServerStarted,
    Connected,
    Disconnected,
    Data,
    ConnectionFailed,
  };

  struct Event
  {
    EventType type = EventType::ServerStarted;
    EspBluedroidSppSession session;
    EspBluedroidSppData data;
    EspBluedroidSppConnectionFailure failure;
  };

  bool enqueue(Event event)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (eventCount == SppEventQueueCapacity)
    {
      ++dropped;
      return false;
    }
    const size_t tail = (eventHead + eventCount) % SppEventQueueCapacity;
    events[tail] = std::move(event);
    ++eventCount;
    return true;
  }

  mutable std::mutex mutex;
  Event events[SppEventQueueCapacity];
  size_t eventHead = 0;
  size_t eventCount = 0;
  size_t dropped = 0;
  bool initialized = false;
  bool initializationCompleted = false;
  bool ending = false;
  bool serverStartPending = false;
  bool serverRunning = false;
  String serverName;
  uint8_t serverChannel = 0;
  uint32_t backendHandle = 0;
  EspBluedroidSppSession activeSession;
  EspBluedroidSppSessionId nextSessionId = 1;
  String txQueue[EspBluedroidSpp::WriteQueueCapacity];
  size_t txHead = 0;
  size_t txCount = 0;
  size_t droppedWrites = 0;
  bool txInFlight = false;
  bool txCongested = false;
  uint8_t rxBuffer[EspBluedroidSpp::ReceiveBufferCapacity] = {};
  size_t rxHead = 0;
  size_t rxCount = 0;
  size_t droppedReceiveBytes = 0;
  bool connecting = false;
  String connectAddress;
  esp_bd_addr_t connectBackendAddress = {};
  uint32_t connectDeadlineMs = 0;
};

#if defined(CONFIG_BT_SPP_ENABLED)
namespace
{
std::atomic<EspBluedroidSppImpl *> activeSpp{nullptr};

void startNextSppWrite(EspBluedroidSppImpl *impl)
{
  uint32_t handle = 0;
  uint8_t *data = nullptr;
  size_t length = 0;
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (
      impl->backendHandle == 0 || impl->txCount == 0 ||
      impl->txInFlight || impl->txCongested)
    {
      return;
    }
    String &value = impl->txQueue[impl->txHead];
    handle = impl->backendHandle;
    data = reinterpret_cast<uint8_t *>(
      const_cast<char *>(value.c_str()));
    length = value.length();
    impl->txInFlight = true;
  }
  if (esp_spp_write(handle, length, data) != ESP_OK)
  {
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      impl->txQueue[impl->txHead] = "";
      impl->txHead =
        (impl->txHead + 1) % EspBluedroidSpp::WriteQueueCapacity;
      --impl->txCount;
      ++impl->droppedWrites;
      impl->txInFlight = false;
    }
    startNextSppWrite(impl);
  }
}

void failSppConnection(
  EspBluedroidSppImpl *impl,
  EspBleError error,
  const char *detail)
{
  EspBluedroidSppConnectionFailure failure;
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (!impl->connecting) return;
    failure.peerAddress = impl->connectAddress;
    failure.error = error;
    failure.detail = detail == nullptr ? "" : detail;
    impl->connecting = false;
    impl->connectAddress = "";
    impl->connectDeadlineMs = 0;
  }
  EspBluedroidSppImpl::Event queued;
  queued.type = EspBluedroidSppImpl::EventType::ConnectionFailed;
  queued.failure = std::move(failure);
  impl->enqueue(std::move(queued));
}

void startPendingSppServer(EspBluedroidSppImpl *impl)
{
  const char *name = nullptr;
  uint8_t channel = 0;
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (!impl->initialized || !impl->serverStartPending || impl->ending)
    {
      return;
    }
    name = impl->serverName.c_str();
    channel = impl->serverChannel;
  }
  if (
    esp_bt_gap_set_scan_mode(
      ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE) != ESP_OK ||
    esp_spp_start_srv(
        ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, channel, name) != ESP_OK)
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->serverStartPending = false;
  }
}

void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *parameter)
{
  EspBluedroidSppImpl *impl = activeSpp.load(std::memory_order_acquire);
  if (impl == nullptr || parameter == nullptr) return;

  if (event == ESP_SPP_INIT_EVT)
  {
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      impl->initialized = parameter->init.status == ESP_SPP_SUCCESS;
      impl->initializationCompleted = true;
    }
    if (parameter->init.status == ESP_SPP_SUCCESS)
    {
      startPendingSppServer(impl);
    }
  }
  else if (event == ESP_SPP_UNINIT_EVT)
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->initialized = false;
  }
  else if (event == ESP_SPP_START_EVT)
  {
    if (parameter->start.status != ESP_SPP_SUCCESS)
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      impl->serverStartPending = false;
      impl->serverRunning = false;
      return;
    }
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      impl->serverStartPending = false;
      impl->serverRunning = true;
      impl->serverChannel = parameter->start.scn;
    }
    EspBluedroidSppImpl::Event queued;
    queued.type = EspBluedroidSppImpl::EventType::ServerStarted;
    impl->enqueue(std::move(queued));
  }
  else if (event == ESP_SPP_DISCOVERY_COMP_EVT)
  {
    bool connecting = false;
    esp_bd_addr_t address = {};
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      connecting = impl->connecting;
      memcpy(address, impl->connectBackendAddress, sizeof(address));
    }
    if (!connecting) return;
    if (
      parameter->disc_comp.status != ESP_SPP_SUCCESS ||
      parameter->disc_comp.scn_num == 0)
    {
      failSppConnection(
        impl, EspBleError::NotFound, "peer does not advertise an SPP service");
      return;
    }
    if (esp_spp_connect(
          ESP_SPP_SEC_NONE, ESP_SPP_ROLE_MASTER,
          parameter->disc_comp.scn[0], address) != ESP_OK)
    {
      failSppConnection(
        impl, EspBleError::BackendFailure,
        "failed to start the SPP connection");
    }
  }
  else if (
    event == ESP_SPP_CL_INIT_EVT &&
    parameter->cl_init.status != ESP_SPP_SUCCESS)
  {
    failSppConnection(
      impl, EspBleError::BackendFailure,
      "failed to initialize the SPP client connection");
  }
  else if (event == ESP_SPP_OPEN_EVT)
  {
    if (parameter->open.status != ESP_SPP_SUCCESS)
    {
      failSppConnection(
        impl, EspBleError::BackendFailure, "SPP connection failed");
      return;
    }
    EspBluedroidSppSession session;
    bool accept = false;
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      if (impl->connecting && impl->backendHandle == 0)
      {
        session.id = impl->nextSessionId++;
        if (impl->nextSessionId == 0) impl->nextSessionId = 1;
        session.peerAddress = classicAddress(parameter->open.rem_bda);
        session.incoming = false;
        impl->backendHandle = parameter->open.handle;
        impl->activeSession = session;
        impl->rxHead = 0;
        impl->rxCount = 0;
        impl->droppedReceiveBytes = 0;
        impl->connecting = false;
        impl->connectAddress = "";
        impl->connectDeadlineMs = 0;
        accept = true;
      }
    }
    if (!accept)
    {
      esp_spp_disconnect(parameter->open.handle);
      return;
    }
    EspBluedroidSppImpl::Event queued;
    queued.type = EspBluedroidSppImpl::EventType::Connected;
    queued.session = std::move(session);
    impl->enqueue(std::move(queued));
  }
  else if (
    event == ESP_SPP_SRV_OPEN_EVT &&
    parameter->srv_open.status == ESP_SPP_SUCCESS)
  {
    EspBluedroidSppSession session;
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      if (impl->backendHandle != 0 || impl->connecting)
      {
        esp_spp_disconnect(parameter->srv_open.handle);
        return;
      }
      session.id = impl->nextSessionId++;
      if (impl->nextSessionId == 0) impl->nextSessionId = 1;
      session.peerAddress = classicAddress(parameter->srv_open.rem_bda);
      session.incoming = true;
      impl->backendHandle = parameter->srv_open.handle;
      impl->activeSession = session;
      impl->rxHead = 0;
      impl->rxCount = 0;
      impl->droppedReceiveBytes = 0;
    }
    EspBluedroidSppImpl::Event queued;
    queued.type = EspBluedroidSppImpl::EventType::Connected;
    queued.session = std::move(session);
    impl->enqueue(std::move(queued));
  }
  else if (
    event == ESP_SPP_DATA_IND_EVT &&
    parameter->data_ind.status == ESP_SPP_SUCCESS)
  {
    EspBluedroidSppSessionId sessionId = 0;
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      if (parameter->data_ind.handle != impl->backendHandle) return;
      sessionId = impl->activeSession.id;
      for (size_t index = 0; index < parameter->data_ind.len; ++index)
      {
        if (impl->rxCount == EspBluedroidSpp::ReceiveBufferCapacity)
        {
          ++impl->droppedReceiveBytes;
          continue;
        }
        const size_t tail =
          (impl->rxHead + impl->rxCount) %
          EspBluedroidSpp::ReceiveBufferCapacity;
        impl->rxBuffer[tail] = parameter->data_ind.data[index];
        ++impl->rxCount;
      }
    }
    EspBluedroidSppImpl::Event queued;
    queued.type = EspBluedroidSppImpl::EventType::Data;
    queued.data.sessionId = sessionId;
    queued.data.value = String(
      reinterpret_cast<const char *>(parameter->data_ind.data),
      parameter->data_ind.len);
    impl->enqueue(std::move(queued));
  }
  else if (event == ESP_SPP_WRITE_EVT)
  {
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      if (
        parameter->write.handle != impl->backendHandle ||
        !impl->txInFlight || impl->txCount == 0)
      {
        return;
      }
      if (parameter->write.status != ESP_SPP_SUCCESS)
      {
        ++impl->droppedWrites;
      }
      impl->txQueue[impl->txHead] = "";
      impl->txHead =
        (impl->txHead + 1) % EspBluedroidSpp::WriteQueueCapacity;
      --impl->txCount;
      impl->txInFlight = false;
      impl->txCongested = parameter->write.cong;
    }
    startNextSppWrite(impl);
  }
  else if (event == ESP_SPP_CONG_EVT)
  {
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      if (parameter->cong.handle != impl->backendHandle) return;
      impl->txCongested = parameter->cong.cong;
    }
    if (!parameter->cong.cong) startNextSppWrite(impl);
  }
  else if (event == ESP_SPP_CLOSE_EVT)
  {
    EspBluedroidSppSession session;
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      if (parameter->close.handle != impl->backendHandle) return;
      session = impl->activeSession;
      impl->backendHandle = 0;
      impl->activeSession = EspBluedroidSppSession();
      for (String &value : impl->txQueue) value = "";
      impl->txHead = 0;
      impl->txCount = 0;
      impl->txInFlight = false;
      impl->txCongested = false;
      impl->rxHead = 0;
      impl->rxCount = 0;
    }
    EspBluedroidSppImpl::Event queued;
    queued.type = EspBluedroidSppImpl::EventType::Disconnected;
    queued.session = std::move(session);
    impl->enqueue(std::move(queued));
  }
  else if (event == ESP_SPP_SRV_STOP_EVT)
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->serverRunning = false;
  }
}
} // namespace
#endif

struct EspBleConnectionImpl
{
  static constexpr size_t EventCapacity = 8;
  static constexpr uint32_t ConnectWaitSliceMilliseconds = 1000;

  enum class EventType : uint8_t
  {
    Connected,
    Disconnected,
    Failed,
    SecurityChanged,
    PasskeyDisplayed,
    NumericComparison,
    GattResult,
    Notification,
  };
  struct Event
  {
    EventType type = EventType::Connected;
    EspBleConnection connection;
    EspBleConnectionFailure failure;
    EspBleSecurityChanged securityChanged;
    EspBlePasskeyDisplayed passkeyDisplayed;
    EspBleGattResult gattResult;
    EspBleGattNotification notification;
  };

  struct GattDatabaseSnapshot
  {
    EspBleConnectionId connectionId = 0;
    EspBleGattServiceInfo services[EspBleBluedroid::MaxDiscoveredGattServices];
    EspBleGattCharacteristicInfo
      characteristics[EspBleBluedroid::MaxDiscoveredGattCharacteristics];
    EspBleGattDescriptorInfo
      descriptors[EspBleBluedroid::MaxDiscoveredGattDescriptors];
    size_t serviceCount = 0;
    size_t characteristicCount = 0;
    size_t descriptorCount = 0;
  };

  class ClientCallbacks : public BLEClientCallbacks
  {
  public:
    explicit ClientCallbacks(EspBleConnectionImpl *owner) : owner_(owner) {}
    void onConnect(BLEClient *client) override
    {
      owner_->backendConnected(client);
    }
    void onDisconnect(BLEClient *) override
    {
      owner_->backendDisconnected();
    }
  private:
    EspBleConnectionImpl *owner_;
  };

  class SecurityCallbacks : public BLESecurityCallbacks
  {
  public:
    explicit SecurityCallbacks(EspBleConnectionImpl *owner) : owner_(owner) {}

    void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override
    {
      owner_->backendSecurityChanged(result);
    }

    uint32_t onPassKeyRequest() override
    {
      return owner_->requestPasskey();
    }

    void onPassKeyNotify(uint32_t passkey) override
    {
      owner_->backendPasskeyDisplayed(passkey);
    }

    bool onConfirmPIN(uint32_t pin) override
    {
      return owner_->requestNumericComparison(pin);
    }

  private:
    EspBleConnectionImpl *owner_;
  };

  EspBleConnectionImpl() : callbacks(this), securityCallbacks(this) {}
  ~EspBleConnectionImpl()
  {
    delete gattDatabase;
    delete securityBackend;
  }

  bool pushEventLocked(const Event &event)
  {
    if (eventCount == EventCapacity)
    {
      ++droppedEvents;
      return false;
    }
    events[(eventHead + eventCount) % EventCapacity] = event;
    ++eventCount;
    return true;
  }

  uint32_t requestPasskey()
  {
    uint32_t responseTimeoutMilliseconds;
    {
      std::lock_guard<std::mutex> lock(passkeyMutex);
      if (staticPasskeyEnabled) return staticPasskey;
    }
    {
      std::lock_guard<std::mutex> lock(mutex);
      responseTimeoutMilliseconds = securityResponseTimeoutMilliseconds;
    }

    const uint32_t startedAt = millis();
    while (millis() - startedAt < responseTimeoutMilliseconds)
    {
      {
        std::lock_guard<std::mutex> lock(passkeyMutex);
        if (passkeyProvided)
        {
          passkeyProvided = false;
          return providedPasskey;
        }
      }
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (ending || securityInputCancelled) return 0;
      }
      vTaskDelay(1);
    }
    return 0;
  }

  bool requestNumericComparison(uint32_t pin)
  {
    uint32_t responseTimeoutMilliseconds;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (ending || !active) return false;
      responseTimeoutMilliseconds = securityResponseTimeoutMilliseconds;
      Event event;
      event.type = EventType::NumericComparison;
      event.passkeyDisplayed.connection = connection;
      event.passkeyDisplayed.passkey = pin;
      pushEventLocked(event);
    }

    const uint32_t startedAt = millis();
    while (millis() - startedAt < responseTimeoutMilliseconds)
    {
      {
        std::lock_guard<std::mutex> lock(passkeyMutex);
        if (numericComparisonConfirmed)
        {
          numericComparisonConfirmed = false;
          return numericComparisonAccept;
        }
      }
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (ending || securityInputCancelled) return false;
      }
      vTaskDelay(1);
    }
    return false;
  }

  void backendConnected(BLEClient *connectedClient)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (ending || active)
    {
      return;
    }
    active = true;
    securityInputCancelled = false;
    connection = EspBleConnection();
    connection.id = nextConnectionId++;
    if (nextConnectionId == 0) nextConnectionId = 1;
    connection.handle = connectedClient->getConnId();
    connection.peerAddress = target.address;
    connection.peerAddressType = target.addressType;
    connection.localRole = EspBleRole::Central;
    connection.mtu = connectedClient->getMTU();
    Event event;
    event.type = EventType::Connected;
    event.connection = connection;
    pushEventLocked(event);
  }

  void backendDisconnected()
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!active)
    {
      return;
    }
    Event event;
    event.type = EventType::Disconnected;
    event.connection = connection;
    active = false;
    connection = EspBleConnection();
    if (gattDatabase != nullptr)
    {
      gattDatabase->connectionId = 0;
      gattDatabase->serviceCount = 0;
      gattDatabase->characteristicCount = 0;
      gattDatabase->descriptorCount = 0;
    }
    if (!ending)
    {
      pushEventLocked(event);
    }
  }

  void backendSecurityChanged(const esp_ble_auth_cmpl_t &result)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (ending || !active) return;
    connection.encrypted = result.success;
    connection.authenticated = result.success &&
      (result.auth_mode & ESP_LE_AUTH_REQ_MITM) != 0;
    connection.bonded = result.success &&
      (result.auth_mode & ESP_LE_AUTH_BOND) != 0;
    connection.encryptionKeySize = result.success ? 16 : 0;

    if (result.success && connection.bonded)
    {
      int count = esp_ble_get_bond_device_num();
      esp_ble_bond_dev_t *bonds = count > 0
        ? new (std::nothrow) esp_ble_bond_dev_t[count] : nullptr;
      if (bonds != nullptr)
      {
        int listed = count;
        if (esp_ble_get_bond_device_list(&listed, bonds) == ESP_OK)
        {
          for (int index = 0; index < listed; ++index)
          {
            if (memcmp(bonds[index].bd_addr, result.bd_addr,
                  sizeof(esp_bd_addr_t)) == 0 &&
                (bonds[index].bond_key.key_mask & ESP_LE_KEY_PENC) != 0)
            {
              connection.encryptionKeySize =
                bonds[index].bond_key.penc_key.key_size;
              break;
            }
          }
        }
        delete[] bonds;
      }
    }

    Event event;
    event.type = EventType::SecurityChanged;
    event.securityChanged.connection = connection;
    event.securityChanged.success = result.success;
    if (!result.success)
    {
      event.securityChanged.error = EspBleError::BackendFailure;
      event.securityChanged.detail =
        String("BLE authentication failed: ") +
        String(static_cast<unsigned>(result.fail_reason));
    }
    pushEventLocked(event);
  }

  void backendPasskeyDisplayed(uint32_t passkey)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (ending || !active) return;
    Event event;
    event.type = EventType::PasskeyDisplayed;
    event.passkeyDisplayed.connection = connection;
    event.passkeyDisplayed.passkey = passkey;
    pushEventLocked(event);
  }

  void queueNotification(
    EspBleConnectionId connectionId,
    const String &serviceUuid,
    const String &characteristicUuid,
    uint16_t handle,
    const uint8_t *data,
    size_t length,
    bool indication)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (ending || !active || connection.id != connectionId)
    {
      ++droppedEvents;
      return;
    }
    Event event;
    event.type = EventType::Notification;
    event.notification.connectionId = connectionId;
    event.notification.serviceUuid = serviceUuid;
    event.notification.characteristicUuid = characteristicUuid;
    event.notification.handle = handle;
    event.notification.value = length == 0
      ? String() : String(reinterpret_cast<const char *>(data), length);
    event.notification.indication = indication;
    pushEventLocked(event);
  }

  static void connectTaskEntry(void *argument)
  {
    EspBleConnectionImpl *impl =
      static_cast<EspBleConnectionImpl *>(argument);
    EspBleScanResult target;
    uint32_t timeoutMilliseconds;
    BLEClient *client;
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      target = impl->target;
      timeoutMilliseconds = impl->timeoutMilliseconds;
      client = impl->client;
    }

    if (client == nullptr)
    {
      client = BLEDevice::createClient();
      if (client != nullptr)
      {
        client->setClientCallbacks(&impl->callbacks);
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->client = client;
      }
    }

    const uint32_t startedAt = millis();
    bool connected = false;
    bool cancelled = false;
    while (client != nullptr)
    {
      uint32_t elapsed;
      {
        std::lock_guard<std::mutex> lock(impl->mutex);
        cancelled = impl->ending;
        elapsed = static_cast<uint32_t>(millis() - startedAt);
      }
      if (cancelled || elapsed >= timeoutMilliseconds)
      {
        break;
      }

      const uint32_t remaining = timeoutMilliseconds - elapsed;
      const uint32_t waitSlice = remaining < ConnectWaitSliceMilliseconds
        ? remaining : ConnectWaitSliceMilliseconds;
      const uint32_t attemptStartedAt = millis();
      connected = client->connect(
        BLEAddress(target.address, static_cast<uint8_t>(target.addressType)),
        static_cast<uint8_t>(target.addressType), waitSlice);
      if (connected)
      {
        break;
      }

      // A prompt failure is a backend rejection, not a timeout worth retrying.
      // A full slice means BLEClient timed out and safely cleaned up its GATT app;
      // retry it so end() only ever waits for one bounded slice.
      const uint32_t attemptElapsed =
        static_cast<uint32_t>(millis() - attemptStartedAt);
      if (attemptElapsed + 20 < waitSlice)
      {
        break;
      }
      delay(10);
    }
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      cancelled = impl->ending;
    }
    if (connected)
    {
      if (cancelled)
      {
        client->disconnect();
      }
      else
      {
        impl->backendConnected(client);
      }
    }
    else if (!cancelled)
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      Event event;
      event.type = EventType::Failed;
      event.failure.peerAddress = target.address;
      event.failure.error = client == nullptr
        ? EspBleError::ResourceExhausted
        : (static_cast<uint32_t>(millis() - startedAt) >= timeoutMilliseconds
            ? EspBleError::Timeout : EspBleError::BackendFailure);
      event.failure.detail = client == nullptr
        ? "failed to create BLE client" : "BLE connection failed";
      impl->pushEventLocked(event);
    }

    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      impl->connecting = false;
      impl->connectTask = nullptr;
    }
    vTaskDelete(nullptr);
  }

  static void gattTaskEntry(void *argument)
  {
    EspBleConnectionImpl *impl =
      static_cast<EspBleConnectionImpl *>(argument);
    EspBleGattResult result;
    BLEClient *client = nullptr;
    String writeValue;
    GattDatabaseSnapshot *discoveredDatabase = nullptr;
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      result.operation = impl->gattOperation;
      result.connectionId = impl->gattConnectionId;
      result.serviceUuid = impl->gattServiceUuid;
      result.characteristicUuid = impl->gattCharacteristicUuid;
      result.descriptorUuid = impl->gattDescriptorUuid;
      result.handle = impl->gattCharacteristicHandle;
      result.response = impl->gattWriteResponse;
      writeValue = impl->gattWriteValue;
      if (impl->active && impl->connection.id == result.connectionId)
      {
        client = impl->client;
      }
    }

    if (client == nullptr || !client->isConnected())
    {
      result.error = EspBleError::InvalidState;
      result.detail = "connection is not an active Central connection";
    }
    else if (result.operation == EspBleGattOperation::DiscoverServices)
    {
      discoveredDatabase = new (std::nothrow) GattDatabaseSnapshot();
      if (discoveredDatabase == nullptr)
      {
        result.error = EspBleError::ResourceExhausted;
        result.detail = "failed to allocate GATT database snapshot";
      }
      else
      {
        discoveredDatabase->connectionId = result.connectionId;
        std::map<std::string, BLERemoteService *> *services =
          client->getServices();
        if (services == nullptr)
        {
          result.error = EspBleError::BackendFailure;
          result.detail = "failed to enumerate GATT services";
        }
        else
        {
          result.success = true;
          for (const auto &serviceItem : *services)
          {
            BLERemoteService *service = serviceItem.second;
            if (service == nullptr) continue;
            if (discoveredDatabase->serviceCount ==
                EspBleBluedroid::MaxDiscoveredGattServices)
            {
              result.success = false;
              result.error = EspBleError::ResourceExhausted;
              result.detail = "too many discovered GATT services";
              break;
            }
            const String serviceUuid = service->getUUID().toString();
            EspBleGattServiceInfo &serviceInfo =
              discoveredDatabase->services[discoveredDatabase->serviceCount++];
            serviceInfo.serviceUuid = serviceUuid;
            serviceInfo.handle = service->getHandle();

            std::map<uint16_t, BLERemoteCharacteristic *> *characteristics =
              service->getCharacteristicsByHandle();
            if (characteristics == nullptr)
            {
              result.success = false;
              result.error = EspBleError::BackendFailure;
              result.detail = "failed to enumerate GATT characteristics";
              break;
            }
            for (const auto &characteristicItem : *characteristics)
            {
              BLERemoteCharacteristic *characteristic =
                characteristicItem.second;
              if (characteristic == nullptr) continue;
              if (discoveredDatabase->characteristicCount ==
                  EspBleBluedroid::MaxDiscoveredGattCharacteristics)
              {
                result.success = false;
                result.error = EspBleError::ResourceExhausted;
                result.detail = "too many discovered GATT characteristics";
                break;
              }
              const String characteristicUuid =
                characteristic->getUUID().toString();
              EspBleGattCharacteristicInfo &characteristicInfo =
                discoveredDatabase->characteristics[
                  discoveredDatabase->characteristicCount++];
              characteristicInfo.serviceUuid = serviceUuid;
              characteristicInfo.characteristicUuid = characteristicUuid;
              characteristicInfo.handle = characteristic->getHandle();
              characteristicInfo.readable = characteristic->canRead();
              characteristicInfo.writable = characteristic->canWrite();
              characteristicInfo.writableWithoutResponse =
                characteristic->canWriteNoResponse();
              characteristicInfo.notifiable = characteristic->canNotify();
              characteristicInfo.indicatable = characteristic->canIndicate();

              std::map<std::string, BLERemoteDescriptor *> *descriptors =
                characteristic->getDescriptors();
              if (descriptors == nullptr) continue;
              for (const auto &descriptorItem : *descriptors)
              {
                BLERemoteDescriptor *descriptor = descriptorItem.second;
                if (descriptor == nullptr) continue;
                if (discoveredDatabase->descriptorCount ==
                    EspBleBluedroid::MaxDiscoveredGattDescriptors)
                {
                  result.success = false;
                  result.error = EspBleError::ResourceExhausted;
                  result.detail = "too many discovered GATT descriptors";
                  break;
                }
                EspBleGattDescriptorInfo &descriptorInfo =
                  discoveredDatabase->descriptors[
                    discoveredDatabase->descriptorCount++];
                descriptorInfo.serviceUuid = serviceUuid;
                descriptorInfo.characteristicUuid = characteristicUuid;
                descriptorInfo.descriptorUuid = descriptor->getUUID().toString();
                descriptorInfo.handle = descriptor->getHandle();
              }
              if (!result.success) break;
            }
            if (!result.success) break;
          }
        }
      }
    }
    else
    {
      BLERemoteCharacteristic *characteristic = nullptr;
      if (result.handle != 0)
      {
        characteristic = findCharacteristicByHandle(
          client, result.handle, result.serviceUuid);
        if (characteristic == nullptr)
        {
          result.error = EspBleError::NotFound;
          result.detail =
            "GATT characteristic handle was not found (discover services first)";
        }
        else
        {
          result.characteristicUuid = characteristic->getUUID().toString();
        }
      }
      else
      {
        BLERemoteService *service = client->getService(result.serviceUuid.c_str());
        if (service == nullptr)
        {
          result.error = EspBleError::NotFound;
          result.detail = "GATT service was not found";
        }
        else
        {
          characteristic =
            service->getCharacteristic(result.characteristicUuid.c_str());
          if (characteristic == nullptr)
          {
            result.error = EspBleError::NotFound;
            result.detail = "GATT characteristic was not found";
          }
        }
      }
      if (characteristic != nullptr)
      {
          result.handle = characteristic->getHandle();
          result.readable = characteristic->canRead();
          result.writable = characteristic->canWrite();
          result.writableWithoutResponse = characteristic->canWriteNoResponse();
          result.notifiable = characteristic->canNotify();
          result.indicatable = characteristic->canIndicate();
          const bool descriptorOperation =
            result.operation == EspBleGattOperation::ReadDescriptor ||
            result.operation == EspBleGattOperation::WriteDescriptor;
          if (descriptorOperation)
          {
            BLERemoteDescriptor *descriptor = characteristic->getDescriptor(
              BLEUUID(result.descriptorUuid.c_str()));
            if (descriptor == nullptr)
            {
              result.error = EspBleError::NotFound;
              result.detail = "GATT descriptor was not found";
            }
            else
            {
              result.handle = descriptor->getHandle();
              if (result.operation == EspBleGattOperation::ReadDescriptor)
              {
                result.value = descriptor->readValue();
                result.success = true;
              }
              else
              {
                result.value = writeValue;
                result.success = descriptor->writeValue(
                  reinterpret_cast<uint8_t *>(
                    const_cast<char *>(writeValue.c_str())),
                  writeValue.length(), result.response);
                if (!result.success)
                {
                  result.error = EspBleError::BackendFailure;
                  result.detail = "GATT descriptor write failed";
                }
              }
            }
          }
          else if (result.operation == EspBleGattOperation::Read && !result.readable)
          {
            result.error = EspBleError::InvalidState;
            result.detail = "GATT characteristic is not readable";
          }
          else if (result.operation == EspBleGattOperation::Read)
          {
            result.value = characteristic->readValue();
            result.success = true;
          }
          else if (result.operation == EspBleGattOperation::Write)
          {
            const bool supported = result.response
              ? result.writable : result.writableWithoutResponse;
            result.value = writeValue;
            if (!supported)
            {
              result.error = EspBleError::InvalidState;
              result.detail = result.response
                ? "GATT characteristic does not support write with response"
                : "GATT characteristic does not support write without response";
            }
            else
            {
              result.success = characteristic->writeValue(
                reinterpret_cast<uint8_t *>(
                  const_cast<char *>(writeValue.c_str())),
                writeValue.length(), result.response);
              if (!result.success)
              {
                result.error = EspBleError::BackendFailure;
                result.detail = "GATT write failed";
              }
            }
          }
          else
          {
            const bool subscribing =
              result.operation == EspBleGattOperation::Subscribe;
            const bool notifications = result.response;
            const bool supported = subscribing &&
              (notifications ? result.notifiable : result.indicatable);
            if (subscribing && !supported)
            {
              result.error = EspBleError::InvalidState;
              result.detail = notifications
                ? "GATT characteristic does not support notifications"
                : "GATT characteristic does not support indications";
            }
            else
            {
              BLERemoteDescriptor *descriptor =
                characteristic->getDescriptor(BLEUUID((uint16_t)0x2902));
              if (descriptor == nullptr)
              {
                result.error = EspBleError::NotFound;
                result.detail = "GATT CCCD was not found";
              }
              else if (subscribing)
              {
                const EspBleConnectionId connectionId = result.connectionId;
                const String serviceUuid = result.serviceUuid;
                const String characteristicUuid = result.characteristicUuid;
                const uint16_t handle = result.handle;
                characteristic->registerForNotify(
                  [impl, connectionId, serviceUuid, characteristicUuid, handle](
                    BLERemoteCharacteristic *,
                    uint8_t *data,
                    size_t length,
                    bool isNotification) {
                    impl->queueNotification(
                      connectionId, serviceUuid, characteristicUuid, handle,
                      data, length, !isNotification);
                  },
                  notifications,
                  false);
                const uint8_t cccd[] = {
                  static_cast<uint8_t>(notifications ? 0x01 : 0x02), 0x00};
                result.success = descriptor->writeValue(
                  const_cast<uint8_t *>(cccd), sizeof(cccd), true);
                if (result.success)
                {
                  result.subscribedToNotifications = notifications;
                  result.subscribedToIndications = !notifications;
                }
                else
                {
                  characteristic->registerForNotify(nullptr, true, false);
                  result.error = EspBleError::BackendFailure;
                  result.detail = "GATT subscription failed";
                }
              }
              else
              {
                const uint8_t cccd[] = {0x00, 0x00};
                result.success = descriptor->writeValue(
                  const_cast<uint8_t *>(cccd), sizeof(cccd), true);
                characteristic->registerForNotify(nullptr, true, false);
                if (!result.success)
                {
                  result.error = EspBleError::BackendFailure;
                  result.detail = "GATT unsubscribe failed";
                }
              }
            }
          }
      }
    }

    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      if (!impl->ending && !impl->gattTimedOut)
      {
        if (result.operation == EspBleGattOperation::DiscoverServices &&
            result.success)
        {
          delete impl->gattDatabase;
          impl->gattDatabase = discoveredDatabase;
          discoveredDatabase = nullptr;
        }
        Event event;
        event.type = EventType::GattResult;
        event.gattResult = result;
        impl->pushEventLocked(event);
      }
      impl->gattOperating = false;
      impl->gattTask = nullptr;
    }
    delete discoveredDatabase;
    vTaskDelete(nullptr);
  }

  mutable std::mutex mutex;
  BLEClient *client = nullptr;
  ClientCallbacks callbacks;
  SecurityCallbacks securityCallbacks;
  BLESecurity *securityBackend = nullptr;
  std::mutex passkeyMutex;
  bool staticPasskeyEnabled = false;
  uint32_t staticPasskey = 0;
  bool passkeyProvided = false;
  uint32_t providedPasskey = 0;
  bool numericComparisonConfirmed = false;
  bool numericComparisonAccept = false;
  bool connecting = false;
  bool ending = false;
  bool active = false;
  bool securityInputCancelled = false;
  uint32_t securityResponseTimeoutMilliseconds = 30000;
  TaskHandle_t connectTask = nullptr;
  bool gattOperating = false;
  TaskHandle_t gattTask = nullptr;
  EspBleConnectionId gattConnectionId = 0;
  EspBleGattOperation gattOperation = EspBleGattOperation::Read;
  String gattServiceUuid;
  String gattCharacteristicUuid;
  String gattDescriptorUuid;
  uint16_t gattCharacteristicHandle = 0;
  String gattWriteValue;
  bool gattWriteResponse = true;
  uint32_t gattStartedAt = 0;
  uint32_t gattTimeoutMilliseconds = 10000;
  bool gattTimedOut = false;
  GattDatabaseSnapshot *gattDatabase = nullptr;
  EspBleScanResult target;
  uint32_t timeoutMilliseconds = 10000;
  EspBleConnection connection;
  EspBleConnectionId nextConnectionId = 1;
  Event events[EventCapacity];
  size_t eventHead = 0;
  size_t eventCount = 0;
  size_t droppedEvents = 0;
};

bool EspBleScanResult::hasName() const
{
  return !name.isEmpty();
}

bool EspBleScanResult::hasManufacturerData() const
{
  return !manufacturerData.isEmpty();
}

bool EspBleScanResult::advertisesService(const char *uuid) const
{
  for (size_t index = 0; index < serviceUuidCount; ++index)
  {
    if (uuidEquals(serviceUuids[index], uuid))
    {
      return true;
    }
  }
  return false;
}

size_t EspBleConnection::maximumNotificationPayload() const
{
  return mtu > 3 ? mtu - 3 : 0;
}

EspBleAdvertising::EspBleAdvertising(EspBleBluedroid *owner) : owner_(owner) {}

void EspBleAdvertising::clear()
{
  name_ = "";
  manufacturerData_ = "";
  serviceUuidCount_ = 0;
  appearance_ = 0;
  scanResponseEnabled_ = true;
  connectable_ = true;
  intervalMinMs_ = 0;
  intervalMaxMs_ = 0;
}

void EspBleAdvertising::setName(const char *name)
{
  name_ = name == nullptr ? "" : name;
}

bool EspBleAdvertising::addServiceUuid(const char *uuid)
{
  if (uuid == nullptr || uuid[0] == '\0')
  {
    owner_->setError(EspBleError::InvalidArgument, "service UUID is empty");
    return false;
  }
  for (size_t index = 0; index < serviceUuidCount_; ++index)
  {
    if (uuidEquals(serviceUuids_[index], uuid))
    {
      owner_->clearError();
      return true;
    }
  }
  if (serviceUuidCount_ == MaxServiceUuids)
  {
    owner_->setError(
      EspBleError::ResourceExhausted, "too many advertising service UUIDs");
    return false;
  }
  serviceUuids_[serviceUuidCount_++] = uuid;
  owner_->clearError();
  return true;
}

void EspBleAdvertising::setManufacturerData(const uint8_t *data, size_t length)
{
  manufacturerData_ = data == nullptr || length == 0
    ? String()
    : String(reinterpret_cast<const char *>(data), length);
}

void EspBleAdvertising::setAppearance(uint16_t appearance)
{
  appearance_ = appearance;
}

void EspBleAdvertising::setScanResponseEnabled(bool enabled)
{
  scanResponseEnabled_ = enabled;
}

void EspBleAdvertising::setConnectable(bool connectable)
{
  connectable_ = connectable;
}

bool EspBleAdvertising::setInterval(
  uint16_t minMilliseconds, uint16_t maxMilliseconds)
{
  if (minMilliseconds < 20 || maxMilliseconds > 10240 ||
      minMilliseconds > maxMilliseconds)
  {
    owner_->setError(
      EspBleError::InvalidArgument,
      "advertising interval must be 20..10240 ms with min <= max");
    return false;
  }
  intervalMinMs_ = minMilliseconds;
  intervalMaxMs_ = maxMilliseconds;
  owner_->clearError();
  return true;
}

bool EspBleAdvertising::start(uint32_t durationSeconds)
{
  if (!owner_->initialized())
  {
    owner_->setError(EspBleError::InvalidState, "BLE stack is not initialized");
    return false;
  }

  BLEAdvertising *backend = BLEDevice::getAdvertising();
  backend->reset();

  BLEAdvertisementData advertisingData;
  if (!appendAdvertisingData(
        advertisingData, ESP_BLE_AD_TYPE_FLAG, String("\x06", 1)))
  {
    owner_->setError(
      EspBleError::InvalidArgument,
      "advertising flags do not fit in legacy advertising payload");
    return false;
  }
  if (appearance_ != 0)
  {
    const String appearance(
      reinterpret_cast<const char *>(&appearance_), sizeof(appearance_));
    if (!appendAdvertisingData(
          advertisingData, ESP_BLE_AD_TYPE_APPEARANCE, appearance))
    {
      owner_->setError(
        EspBleError::InvalidArgument,
        "appearance does not fit in legacy advertising payload");
      return false;
    }
  }
  if (serviceUuidCount_ > 0)
  {
    String uuids16;
    String uuids32;
    String uuids128;
    for (size_t index = 0; index < serviceUuidCount_; ++index)
    {
      BLEUUID uuid(serviceUuids_[index].c_str());
      switch (uuid.bitSize())
      {
      case 16:
        uuids16 += String(reinterpret_cast<const char *>(
          &uuid.getNative()->uuid.uuid16), 2);
        break;
      case 32:
        uuids32 += String(reinterpret_cast<const char *>(
          &uuid.getNative()->uuid.uuid32), 4);
        break;
      case 128:
        uuids128 += String(reinterpret_cast<const char *>(
          uuid.getNative()->uuid.uuid128), 16);
        break;
      default:
        owner_->setError(
          EspBleError::InvalidArgument, "invalid advertising service UUID");
        return false;
      }
    }

    const struct
    {
      const String *values;
      uint8_t type;
    } lists[] = {
      {&uuids16, ESP_BLE_AD_TYPE_16SRV_CMPL},
      {&uuids32, ESP_BLE_AD_TYPE_32SRV_CMPL},
      {&uuids128, ESP_BLE_AD_TYPE_128SRV_CMPL},
    };
    for (const auto &list : lists)
    {
      if (!list.values->isEmpty() &&
          !appendAdvertisingData(advertisingData, list.type, *list.values))
      {
        owner_->setError(
          EspBleError::InvalidArgument,
          "service UUIDs do not fit in legacy advertising payload");
        return false;
      }
    }
  }
  if (!manufacturerData_.isEmpty())
  {
    if (!appendAdvertisingData(
          advertisingData, ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE,
          manufacturerData_))
    {
      owner_->setError(
        EspBleError::InvalidArgument,
        "manufacturer data does not fit in legacy advertising payload");
      return false;
    }
  }
  if (!scanResponseEnabled_ && !name_.isEmpty())
  {
    if (!appendAdvertisingData(
          advertisingData, ESP_BLE_AD_TYPE_NAME_CMPL, name_))
    {
      owner_->setError(
        EspBleError::InvalidArgument,
        "name does not fit in legacy advertising payload");
      return false;
    }
  }
  if (!backend->setAdvertisementData(advertisingData))
  {
    owner_->setError(
      EspBleError::BackendFailure, "failed to configure advertising data");
    return false;
  }

  backend->setScanResponse(scanResponseEnabled_);
  if (scanResponseEnabled_)
  {
    BLEAdvertisementData responseData;
    if (!name_.isEmpty() &&
        !appendAdvertisingData(
          responseData, ESP_BLE_AD_TYPE_NAME_CMPL, name_))
    {
      owner_->setError(
        EspBleError::InvalidArgument,
        "name does not fit in legacy scan response payload");
      return false;
    }
    if (!backend->setScanResponseData(responseData))
    {
      owner_->setError(
        EspBleError::BackendFailure, "failed to configure scan response data");
      return false;
    }
  }

  backend->setAdvertisementType(
    connectable_ ? ADV_TYPE_IND
      : (scanResponseEnabled_ ? ADV_TYPE_SCAN_IND : ADV_TYPE_NONCONN_IND));
  if (intervalMinMs_ != 0 && intervalMaxMs_ != 0)
  {
    backend->setMinInterval(static_cast<uint16_t>(
      (static_cast<uint32_t>(intervalMinMs_) * 8) / 5));
    backend->setMaxInterval(static_cast<uint16_t>(
      (static_cast<uint32_t>(intervalMaxMs_) * 8) / 5));
  }
  if (!backend->start())
  {
    owner_->setError(EspBleError::BackendFailure, "failed to start advertising");
    return false;
  }

  advertising_ = true;
  startedAtMs_ = millis();
  durationMs_ = durationSeconds == 0 ? 0 : durationSeconds * 1000UL;
  owner_->clearError();
  return true;
}

bool EspBleAdvertising::stop()
{
  if (!owner_->initialized())
  {
    owner_->setError(EspBleError::InvalidState, "BLE stack is not initialized");
    return false;
  }
  if (!advertising_)
  {
    owner_->clearError();
    return true;
  }
  if (!BLEDevice::getAdvertising()->stop())
  {
    owner_->setError(EspBleError::BackendFailure, "failed to stop advertising");
    return false;
  }
  advertising_ = false;
  durationMs_ = 0;
  owner_->clearError();
  return true;
}

bool EspBleAdvertising::isAdvertising() const
{
  return owner_->initialized() && advertising_;
}

void EspBleAdvertising::update()
{
  if (advertising_ && durationMs_ != 0 &&
      static_cast<uint32_t>(millis() - startedAtMs_) >= durationMs_)
  {
    stop();
  }
}

EspBleScanner::EspBleScanner(EspBleBluedroid *owner) : owner_(owner) {}

EspBleScanner::~EspBleScanner()
{
  delete impl_;
}

void EspBleScanner::onResult(ResultCallback callback)
{
  resultCallback_ = std::move(callback);
}

bool EspBleScanner::start(const EspBleScanConfig &config)
{
  if (!owner_->initialized())
  {
    owner_->setError(EspBleError::InvalidState, "BLE stack is not initialized");
    return false;
  }
  if (config.intervalMilliseconds == 0 || config.windowMilliseconds == 0 ||
      config.windowMilliseconds > config.intervalMilliseconds)
  {
    owner_->setError(
      EspBleError::InvalidArgument,
      "scan window must be nonzero and no greater than interval");
    return false;
  }
  if (impl_ == nullptr)
  {
    impl_ = new (std::nothrow) EspBleScannerImpl();
    if (impl_ == nullptr)
    {
      owner_->setError(
        EspBleError::ResourceExhausted, "failed to allocate scanner state");
      return false;
    }
  }

  BLEScan *backend = BLEDevice::getScan();
  backend->setAdvertisedDeviceCallbacks(
    &impl_->callbacks, config.wantDuplicates, true);
  backend->setActiveScan(config.active);
  backend->setInterval(config.intervalMilliseconds);
  backend->setWindow(config.windowMilliseconds);
  if (!backend->start(config.durationSeconds, nullptr, false))
  {
    owner_->setError(EspBleError::BackendFailure, "failed to start scan");
    return false;
  }
  owner_->clearError();
  return true;
}

bool EspBleScanner::stop()
{
  if (!owner_->initialized())
  {
    owner_->setError(EspBleError::InvalidState, "BLE stack is not initialized");
    return false;
  }
  BLEScan *backend = BLEDevice::getScan();
  if (!backend->isScanning())
  {
    owner_->clearError();
    return true;
  }
  if (!backend->stop())
  {
    owner_->setError(EspBleError::BackendFailure, "failed to stop scan");
    return false;
  }
  owner_->clearError();
  return true;
}

bool EspBleScanner::isScanning() const
{
  return owner_->initialized() && BLEDevice::getScan()->isScanning();
}

size_t EspBleScanner::droppedResultCount() const
{
  if (impl_ == nullptr)
  {
    return 0;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->dropped;
}

#ifdef ESP_BLE_BLUEDROID_TESTING
bool EspBleScanner::injectResultForTest(const EspBleScanResult &result)
{
  if (impl_ == nullptr)
  {
    impl_ = new (std::nothrow) EspBleScannerImpl();
    if (impl_ == nullptr) return false;
  }
  return impl_->enqueue(result);
}

size_t EspBleScanner::pendingResultCountForTest() const
{
  if (impl_ == nullptr) return 0;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->count;
}
#endif

void EspBleScanner::flushPendingResults()
{
  if (impl_ == nullptr)
  {
    return;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->head = 0;
  impl_->count = 0;
  impl_->dropped = 0;
}

void EspBleScanner::dispatchPendingResults()
{
  if (impl_ == nullptr || !resultCallback_)
  {
    return;
  }
  while (true)
  {
    EspBleScanResult result;
    {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      if (impl_->count == 0)
      {
        break;
      }
      result = std::move(impl_->queue[impl_->head]);
      impl_->head = (impl_->head + 1) % ScanQueueCapacity;
      --impl_->count;
    }
    resultCallback_(result);
  }
}

EspBluedroidClassicInquiry::EspBluedroidClassicInquiry(
  EspBleBluedroid *owner)
    : owner_(owner)
{
}

EspBluedroidClassicInquiry::~EspBluedroidClassicInquiry()
{
  end();
  delete impl_;
}

void EspBluedroidClassicInquiry::onResult(ResultCallback callback)
{
  resultCallback_ = std::move(callback);
}

void EspBluedroidClassicInquiry::onComplete(CompleteCallback callback)
{
  completeCallback_ = std::move(callback);
}

bool EspBluedroidClassicInquiry::begin(const char *deviceName)
{
#if defined(CONFIG_BT_CLASSIC_ENABLED)
  if (impl_ == nullptr)
  {
    impl_ = new (std::nothrow) EspBluedroidClassicInquiryImpl();
    if (impl_ == nullptr)
    {
      owner_->setError(
        EspBleError::ResourceExhausted,
        "failed to allocate Classic Inquiry state");
      return false;
    }
  }
  activeClassicInquiry.store(impl_, std::memory_order_release);
  if (esp_bt_gap_register_callback(classicGapCallback) != ESP_OK)
  {
    activeClassicInquiry.store(nullptr, std::memory_order_release);
    owner_->setError(
      EspBleError::BackendFailure,
      "failed to register the Classic GAP callback");
    return false;
  }
  if (esp_bt_gap_set_device_name(deviceName) != ESP_OK)
  {
    activeClassicInquiry.store(nullptr, std::memory_order_release);
    owner_->setError(
      EspBleError::BackendFailure,
      "failed to set the Classic Bluetooth device name");
    return false;
  }
  return true;
#else
  (void)deviceName;
  return true;
#endif
}

void EspBluedroidClassicInquiry::end()
{
  if (impl_ == nullptr) return;
#if defined(CONFIG_BT_CLASSIC_ENABLED)
  activeClassicInquiry.store(nullptr, std::memory_order_release);
  bool running = false;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    running = impl_->running;
  }
  if (running)
  {
    esp_bt_gap_cancel_discovery();
  }
#endif
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->head = 0;
  impl_->count = 0;
  impl_->dropped = 0;
  impl_->running = false;
  impl_->stopRequested = false;
  impl_->completionPending = false;
  impl_->completionCancelled = false;
}

bool EspBluedroidClassicInquiry::start(
  const EspBluedroidClassicInquiryConfig &config)
{
  if (!owner_->initialized_)
  {
    owner_->setError(
      EspBleError::InvalidState, "Bluetooth stack is not initialized");
    return false;
  }
#if !defined(CONFIG_BT_CLASSIC_ENABLED)
  (void)config;
  owner_->setError(
    EspBleError::Unsupported,
    "Classic Bluetooth is not enabled for this target");
  return false;
#else
  if (config.durationSeconds == 0 || config.durationSeconds > 61)
  {
    owner_->setError(
      EspBleError::InvalidArgument,
      "Classic Inquiry duration must be between 1 and 61 seconds");
    return false;
  }
  if (impl_ == nullptr)
  {
    owner_->setError(
      EspBleError::InvalidState, "Classic Inquiry is not initialized");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->running)
    {
      owner_->setError(
        EspBleError::InvalidState, "Classic Inquiry is already running");
      return false;
    }
    impl_->head = 0;
    impl_->count = 0;
    impl_->dropped = 0;
    impl_->stopRequested = false;
    impl_->completionPending = false;
    impl_->completionCancelled = false;
    impl_->running = true;
  }
  const uint8_t durationUnits = static_cast<uint8_t>(
    (static_cast<uint64_t>(config.durationSeconds) * 100 + 127) / 128);
  const esp_err_t result = esp_bt_gap_start_discovery(
    ESP_BT_INQ_MODE_GENERAL_INQUIRY, durationUnits, config.maxResponses);
  if (result != ESP_OK)
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->running = false;
    owner_->setError(
      EspBleError::BackendFailure, "failed to start Classic Inquiry");
    return false;
  }
  owner_->clearError();
  return true;
#endif
}

bool EspBluedroidClassicInquiry::stop()
{
  if (!owner_->initialized_)
  {
    owner_->setError(
      EspBleError::InvalidState, "Bluetooth stack is not initialized");
    return false;
  }
#if !defined(CONFIG_BT_CLASSIC_ENABLED)
  owner_->setError(
    EspBleError::Unsupported,
    "Classic Bluetooth is not enabled for this target");
  return false;
#else
  if (impl_ == nullptr)
  {
    owner_->setError(
      EspBleError::InvalidState, "Classic Inquiry is not initialized");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->running)
    {
      owner_->clearError();
      return true;
    }
    impl_->stopRequested = true;
  }
  if (esp_bt_gap_cancel_discovery() != ESP_OK)
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->stopRequested = false;
    owner_->setError(
      EspBleError::BackendFailure, "failed to stop Classic Inquiry");
    return false;
  }
  owner_->clearError();
  return true;
#endif
}

bool EspBluedroidClassicInquiry::isRunning() const
{
  if (impl_ == nullptr) return false;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->running;
}

size_t EspBluedroidClassicInquiry::droppedResultCount() const
{
  if (impl_ == nullptr) return 0;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->dropped;
}

void EspBluedroidClassicInquiry::update()
{
  if (impl_ == nullptr) return;
  while (true)
  {
    EspBluedroidClassicInquiryResult result;
    {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      if (impl_->count == 0) break;
      result = std::move(impl_->queue[impl_->head]);
      impl_->head = (impl_->head + 1) % ClassicInquiryQueueCapacity;
      --impl_->count;
    }
    if (resultCallback_) resultCallback_(result);
  }

  EspBluedroidClassicInquiryComplete event;
  bool dispatchComplete = false;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->completionPending)
    {
      event.cancelled = impl_->completionCancelled;
      impl_->completionPending = false;
      dispatchComplete = true;
    }
  }
  if (dispatchComplete && completeCallback_) completeCallback_(event);
}

EspBluedroidSpp::EspBluedroidSpp(EspBleBluedroid *owner)
    : owner_(owner)
{
}

EspBluedroidSpp::~EspBluedroidSpp()
{
  end();
  delete impl_;
}

bool EspBluedroidSpp::begin()
{
#if !defined(CONFIG_BT_SPP_ENABLED)
  return true;
#else
  if (impl_ == nullptr)
  {
    impl_ = new (std::nothrow) EspBluedroidSppImpl();
    if (impl_ == nullptr)
    {
      owner_->setError(
        EspBleError::ResourceExhausted, "failed to allocate SPP state");
      return false;
    }
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ending = false;
    impl_->initialized = false;
    impl_->initializationCompleted = false;
  }
  activeSpp.store(impl_, std::memory_order_release);
  if (esp_spp_register_callback(sppCallback) != ESP_OK)
  {
    activeSpp.store(nullptr, std::memory_order_release);
    owner_->setError(
      EspBleError::BackendFailure, "failed to register the SPP callback");
    return false;
  }
  esp_spp_cfg_t config = BT_SPP_DEFAULT_CONFIG();
  config.mode = ESP_SPP_MODE_CB;
  if (esp_spp_enhanced_init(&config) != ESP_OK)
  {
    activeSpp.store(nullptr, std::memory_order_release);
    owner_->setError(EspBleError::BackendFailure, "failed to initialize SPP");
    return false;
  }
  const uint32_t startedAt = millis();
  while (true)
  {
    bool completed = false;
    bool initialized = false;
    {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      completed = impl_->initializationCompleted;
      initialized = impl_->initialized;
    }
    if (completed)
    {
      if (initialized) return true;
      activeSpp.store(nullptr, std::memory_order_release);
      owner_->setError(
        EspBleError::BackendFailure, "SPP profile initialization failed");
      return false;
    }
    if (millis() - startedAt >= 1000)
    {
      activeSpp.store(nullptr, std::memory_order_release);
      owner_->setError(
        EspBleError::Timeout, "SPP profile initialization timed out");
      return false;
    }
    delay(1);
  }
#endif
}

void EspBluedroidSpp::end()
{
  if (impl_ == nullptr) return;
#if defined(CONFIG_BT_SPP_ENABLED)
  bool initialized = false;
  bool running = false;
  uint32_t handle = 0;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ending = true;
    initialized = impl_->initialized;
    running = impl_->serverRunning;
    handle = impl_->backendHandle;
  }
  if (handle != 0) esp_spp_disconnect(handle);
  if (running) esp_spp_stop_srv();
  if (initialized)
  {
    esp_spp_deinit();
    const uint32_t startedAt = millis();
    while (true)
    {
      {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->initialized) break;
      }
      if (millis() - startedAt >= 1000) break;
      delay(1);
    }
  }
  activeSpp.store(nullptr, std::memory_order_release);
#endif
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->eventHead = 0;
  impl_->eventCount = 0;
  impl_->dropped = 0;
  impl_->initialized = false;
  impl_->initializationCompleted = false;
  impl_->ending = false;
  impl_->serverStartPending = false;
  impl_->serverRunning = false;
  impl_->serverName = "";
  impl_->serverChannel = 0;
  impl_->backendHandle = 0;
  impl_->activeSession = EspBluedroidSppSession();
  impl_->nextSessionId = 1;
  for (String &value : impl_->txQueue) value = "";
  impl_->txHead = 0;
  impl_->txCount = 0;
  impl_->droppedWrites = 0;
  impl_->txInFlight = false;
  impl_->txCongested = false;
  impl_->rxHead = 0;
  impl_->rxCount = 0;
  impl_->droppedReceiveBytes = 0;
  impl_->connecting = false;
  impl_->connectAddress = "";
  memset(impl_->connectBackendAddress, 0, sizeof(impl_->connectBackendAddress));
  impl_->connectDeadlineMs = 0;
}

void EspBluedroidSpp::onServerStarted(ServerStartedCallback callback)
{
  serverStartedCallback_ = std::move(callback);
}

void EspBluedroidSpp::onConnected(SessionCallback callback)
{
  connectedCallback_ = std::move(callback);
}

void EspBluedroidSpp::onDisconnected(SessionCallback callback)
{
  disconnectedCallback_ = std::move(callback);
}

void EspBluedroidSpp::onData(DataCallback callback)
{
  dataCallback_ = std::move(callback);
}

void EspBluedroidSpp::onConnectionFailed(
  ConnectionFailureCallback callback)
{
  connectionFailedCallback_ = std::move(callback);
}

bool EspBluedroidSpp::connect(
  const char *address,
  uint32_t timeoutMilliseconds)
{
  if (!owner_->initialized_)
  {
    owner_->setError(
      EspBleError::InvalidState, "Bluetooth stack is not initialized");
    return false;
  }
#if !defined(CONFIG_BT_SPP_ENABLED)
  (void)address;
  (void)timeoutMilliseconds;
  owner_->setError(EspBleError::Unsupported, "SPP is not enabled");
  return false;
#else
  if (!isValidBleAddress(address) || timeoutMilliseconds == 0)
  {
    owner_->setError(
      EspBleError::InvalidArgument,
      "SPP peer address must be canonical and timeout must be nonzero");
    return false;
  }
  unsigned bytes[ESP_BD_ADDR_LEN] = {};
  if (sscanf(
        address, "%02x:%02x:%02x:%02x:%02x:%02x",
        &bytes[0], &bytes[1], &bytes[2],
        &bytes[3], &bytes[4], &bytes[5]) != ESP_BD_ADDR_LEN)
  {
    owner_->setError(
      EspBleError::InvalidArgument, "invalid SPP peer address");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->connecting || impl_->backendHandle != 0)
    {
      owner_->setError(
        EspBleError::InvalidState,
        "an SPP connection is already pending or active");
      return false;
    }
    impl_->connecting = true;
    impl_->connectAddress = address;
    for (size_t index = 0; index < ESP_BD_ADDR_LEN; ++index)
    {
      impl_->connectBackendAddress[index] =
        static_cast<uint8_t>(bytes[index]);
    }
    impl_->connectDeadlineMs = millis() + timeoutMilliseconds;
  }
  if (esp_spp_start_discovery(impl_->connectBackendAddress) != ESP_OK)
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->connecting = false;
    impl_->connectAddress = "";
    impl_->connectDeadlineMs = 0;
    owner_->setError(
      EspBleError::BackendFailure, "failed to start SPP service discovery");
    return false;
  }
  owner_->clearError();
  return true;
#endif
}

bool EspBluedroidSpp::startServer(
  const EspBluedroidSppServerConfig &config)
{
  if (!owner_->initialized_)
  {
    owner_->setError(
      EspBleError::InvalidState, "Bluetooth stack is not initialized");
    return false;
  }
#if !defined(CONFIG_BT_SPP_ENABLED)
  (void)config;
  owner_->setError(EspBleError::Unsupported, "SPP is not enabled");
  return false;
#else
  if (
    config.serviceName == nullptr || config.serviceName[0] == '\0' ||
    config.channel > ESP_SPP_MAX_SCN)
  {
    owner_->setError(
      EspBleError::InvalidArgument,
      "invalid SPP service name or channel");
    return false;
  }
  if (impl_ == nullptr)
  {
    owner_->setError(EspBleError::InvalidState, "SPP is not initialized");
    return false;
  }
  bool initialized = false;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->serverStartPending || impl_->serverRunning)
    {
      owner_->setError(
        EspBleError::InvalidState, "SPP server is already starting or running");
      return false;
    }
    impl_->serverName = config.serviceName;
    impl_->serverChannel = config.channel;
    impl_->serverStartPending = true;
    initialized = impl_->initialized;
  }
  if (initialized) startPendingSppServer(impl_);
  owner_->clearError();
  return true;
#endif
}

bool EspBluedroidSpp::stopServer()
{
  if (!owner_->initialized_)
  {
    owner_->setError(
      EspBleError::InvalidState, "Bluetooth stack is not initialized");
    return false;
  }
#if !defined(CONFIG_BT_SPP_ENABLED)
  owner_->setError(EspBleError::Unsupported, "SPP is not enabled");
  return false;
#else
  bool running = false;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->serverStartPending)
    {
      impl_->serverStartPending = false;
      owner_->clearError();
      return true;
    }
    running = impl_->serverRunning;
  }
  if (!running)
  {
    owner_->clearError();
    return true;
  }
  if (esp_spp_stop_srv() != ESP_OK)
  {
    owner_->setError(EspBleError::BackendFailure, "failed to stop SPP server");
    return false;
  }
  owner_->clearError();
  return true;
#endif
}

bool EspBluedroidSpp::serverRunning() const
{
  if (impl_ == nullptr) return false;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->serverRunning;
}

size_t EspBluedroidSpp::sessionCount() const
{
  if (impl_ == nullptr) return 0;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->backendHandle == 0 ? 0 : 1;
}

bool EspBluedroidSpp::session(
  EspBluedroidSppSessionId sessionId,
  EspBluedroidSppSession &session) const
{
  if (impl_ == nullptr || sessionId == 0) return false;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (
    impl_->backendHandle == 0 || impl_->activeSession.id != sessionId)
  {
    return false;
  }
  session = impl_->activeSession;
  return true;
}

bool EspBluedroidSpp::write(
  EspBluedroidSppSessionId sessionId,
  const uint8_t *data,
  size_t length)
{
  if (!owner_->initialized_)
  {
    owner_->setError(
      EspBleError::InvalidState, "Bluetooth stack is not initialized");
    return false;
  }
  if (
    data == nullptr || length == 0 ||
    length > EspBluedroidSpp::MaximumWriteSize)
  {
    owner_->setError(
      EspBleError::InvalidArgument,
      "SPP write data must contain between 1 and 990 bytes");
    return false;
  }
#if !defined(CONFIG_BT_SPP_ENABLED)
  (void)sessionId;
  owner_->setError(EspBleError::Unsupported, "SPP is not enabled");
  return false;
#else
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (
      impl_->backendHandle == 0 ||
      impl_->activeSession.id != sessionId)
    {
      owner_->setError(EspBleError::NotFound, "SPP session was not found");
      return false;
    }
    if (impl_->txCount == EspBluedroidSpp::WriteQueueCapacity)
    {
      ++impl_->droppedWrites;
      owner_->setError(
        EspBleError::ResourceExhausted, "SPP write queue is full");
      return false;
    }
    const size_t tail =
      (impl_->txHead + impl_->txCount) %
      EspBluedroidSpp::WriteQueueCapacity;
    impl_->txQueue[tail] = String(
      reinterpret_cast<const char *>(data), length);
    if (impl_->txQueue[tail].length() != length)
    {
      impl_->txQueue[tail] = "";
      owner_->setError(
        EspBleError::ResourceExhausted, "failed to copy SPP write data");
      return false;
    }
    ++impl_->txCount;
  }
  startNextSppWrite(impl_);
  owner_->clearError();
  return true;
#endif
}

bool EspBluedroidSpp::write(
  EspBluedroidSppSessionId sessionId,
  const String &value)
{
  return write(
    sessionId,
    reinterpret_cast<const uint8_t *>(value.c_str()),
    value.length());
}

bool EspBluedroidSpp::disconnect(EspBluedroidSppSessionId sessionId)
{
  if (!owner_->initialized_)
  {
    owner_->setError(
      EspBleError::InvalidState, "Bluetooth stack is not initialized");
    return false;
  }
#if !defined(CONFIG_BT_SPP_ENABLED)
  (void)sessionId;
  owner_->setError(EspBleError::Unsupported, "SPP is not enabled");
  return false;
#else
  uint32_t handle = 0;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (
      impl_->backendHandle == 0 ||
      impl_->activeSession.id != sessionId)
    {
      owner_->setError(EspBleError::NotFound, "SPP session was not found");
      return false;
    }
    handle = impl_->backendHandle;
  }
  if (esp_spp_disconnect(handle) != ESP_OK)
  {
    owner_->setError(
      EspBleError::BackendFailure, "failed to disconnect SPP session");
    return false;
  }
  owner_->clearError();
  return true;
#endif
}

size_t EspBluedroidSpp::pendingWriteCount() const
{
  if (impl_ == nullptr) return 0;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->txCount;
}

size_t EspBluedroidSpp::pendingWriteCount(
  EspBluedroidSppSessionId sessionId) const
{
  if (impl_ == nullptr || sessionId == 0) return 0;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (
    impl_->backendHandle == 0 ||
    impl_->activeSession.id != sessionId)
  {
    return 0;
  }
  return impl_->txCount;
}

size_t EspBluedroidSpp::droppedWriteCount() const
{
  if (impl_ == nullptr) return 0;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->droppedWrites;
}

size_t EspBluedroidSpp::available(
  EspBluedroidSppSessionId sessionId) const
{
  if (impl_ == nullptr || sessionId == 0) return 0;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (
    impl_->backendHandle == 0 ||
    impl_->activeSession.id != sessionId)
  {
    return 0;
  }
  return impl_->rxCount;
}

int EspBluedroidSpp::peek(EspBluedroidSppSessionId sessionId) const
{
  if (impl_ == nullptr || sessionId == 0) return -1;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (
    impl_->backendHandle == 0 ||
    impl_->activeSession.id != sessionId ||
    impl_->rxCount == 0)
  {
    return -1;
  }
  return impl_->rxBuffer[impl_->rxHead];
}

int EspBluedroidSpp::read(EspBluedroidSppSessionId sessionId)
{
  if (impl_ == nullptr || sessionId == 0) return -1;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (
    impl_->backendHandle == 0 ||
    impl_->activeSession.id != sessionId ||
    impl_->rxCount == 0)
  {
    return -1;
  }
  const int value = impl_->rxBuffer[impl_->rxHead];
  impl_->rxHead =
    (impl_->rxHead + 1) % EspBluedroidSpp::ReceiveBufferCapacity;
  --impl_->rxCount;
  return value;
}

size_t EspBluedroidSpp::read(
  EspBluedroidSppSessionId sessionId,
  uint8_t *data,
  size_t length)
{
  if (
    impl_ == nullptr || sessionId == 0 ||
    data == nullptr || length == 0)
  {
    return 0;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (
    impl_->backendHandle == 0 ||
    impl_->activeSession.id != sessionId)
  {
    return 0;
  }
  const size_t count = std::min(length, impl_->rxCount);
  for (size_t index = 0; index < count; ++index)
  {
    data[index] = impl_->rxBuffer[impl_->rxHead];
    impl_->rxHead =
      (impl_->rxHead + 1) % EspBluedroidSpp::ReceiveBufferCapacity;
  }
  impl_->rxCount -= count;
  return count;
}

size_t EspBluedroidSpp::droppedReceiveByteCount() const
{
  if (impl_ == nullptr) return 0;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->droppedReceiveBytes;
}

size_t EspBluedroidSpp::droppedEventCount() const
{
  if (impl_ == nullptr) return 0;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->dropped;
}

EspBluedroidSppStream::EspBluedroidSppStream(
  EspBluedroidSpp &spp,
  EspBluedroidSppSessionId sessionId)
{
  attach(spp, sessionId);
}

bool EspBluedroidSppStream::attach(
  EspBluedroidSpp &spp,
  EspBluedroidSppSessionId sessionId)
{
  EspBluedroidSppSession session;
  if (!spp.session(sessionId, session)) return false;
  spp_ = &spp;
  sessionId_ = sessionId;
  return true;
}

void EspBluedroidSppStream::detach()
{
  spp_ = nullptr;
  sessionId_ = 0;
}

bool EspBluedroidSppStream::connected() const
{
  if (spp_ == nullptr || sessionId_ == 0) return false;
  EspBluedroidSppSession session;
  return spp_->session(sessionId_, session);
}

EspBluedroidSppStream::operator bool() const
{
  return connected();
}

EspBluedroidSppSessionId EspBluedroidSppStream::sessionId() const
{
  return sessionId_;
}

int EspBluedroidSppStream::available()
{
  if (spp_ == nullptr) return 0;
  return static_cast<int>(spp_->available(sessionId_));
}

int EspBluedroidSppStream::peek()
{
  if (spp_ == nullptr) return -1;
  return spp_->peek(sessionId_);
}

int EspBluedroidSppStream::read()
{
  if (spp_ == nullptr) return -1;
  return spp_->read(sessionId_);
}

int EspBluedroidSppStream::availableForWrite()
{
  if (!connected()) return 0;
  const size_t pending = spp_->pendingWriteCount(sessionId_);
  if (pending >= EspBluedroidSpp::WriteQueueCapacity) return 0;
  return static_cast<int>(
    (EspBluedroidSpp::WriteQueueCapacity - pending) *
    EspBluedroidSpp::MaximumWriteSize);
}

void EspBluedroidSppStream::flush()
{
  if (spp_ == nullptr) return;
  while (connected() && spp_->pendingWriteCount(sessionId_) != 0)
  {
    delay(1);
  }
}

size_t EspBluedroidSppStream::write(uint8_t value)
{
  return write(&value, 1);
}

size_t EspBluedroidSppStream::write(
  const uint8_t *data,
  size_t length)
{
  if (
    spp_ == nullptr || !connected() ||
    data == nullptr || length == 0)
  {
    if (length != 0) setWriteError();
    return 0;
  }
  size_t written = 0;
  while (written < length)
  {
    const size_t remaining = length - written;
    const size_t chunk =
      std::min(remaining, EspBluedroidSpp::MaximumWriteSize);
    if (!spp_->write(sessionId_, data + written, chunk))
    {
      setWriteError();
      break;
    }
    written += chunk;
  }
  return written;
}

void EspBluedroidSpp::update()
{
  if (impl_ == nullptr) return;
#if defined(CONFIG_BT_SPP_ENABLED)
  bool timedOut = false;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    timedOut =
      impl_->connecting &&
      static_cast<int32_t>(millis() - impl_->connectDeadlineMs) >= 0;
  }
  if (timedOut)
  {
    failSppConnection(
      impl_, EspBleError::Timeout, "SPP connection timed out");
  }
#endif
  while (true)
  {
    EspBluedroidSppImpl::Event event;
    {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      if (impl_->eventCount == 0) break;
      event = std::move(impl_->events[impl_->eventHead]);
      impl_->eventHead =
        (impl_->eventHead + 1) % SppEventQueueCapacity;
      --impl_->eventCount;
    }
    if (
      event.type == EspBluedroidSppImpl::EventType::ServerStarted &&
      serverStartedCallback_)
    {
      serverStartedCallback_();
    }
    else if (
      event.type == EspBluedroidSppImpl::EventType::Connected &&
      connectedCallback_)
    {
      connectedCallback_(event.session);
    }
    else if (
      event.type == EspBluedroidSppImpl::EventType::Disconnected &&
      disconnectedCallback_)
    {
      disconnectedCallback_(event.session);
    }
    else if (
      event.type == EspBluedroidSppImpl::EventType::Data && dataCallback_)
    {
      dataCallback_(event.data);
    }
    else if (
      event.type == EspBluedroidSppImpl::EventType::ConnectionFailed &&
      connectionFailedCallback_)
    {
      connectionFailedCallback_(event.failure);
    }
  }
}

EspBluedroidClassic::EspBluedroidClassic(EspBleBluedroid *owner)
    : inquiry_(owner), spp_(owner)
{
}

EspBluedroidClassicInquiry &EspBluedroidClassic::inquiry()
{
  return inquiry_;
}

EspBluedroidSpp &EspBluedroidClassic::spp()
{
  return spp_;
}

bool EspBluedroidClassic::begin(const char *deviceName)
{
  if (!inquiry_.begin(deviceName)) return false;
  if (!spp_.begin())
  {
    inquiry_.end();
    return false;
  }
  return true;
}

void EspBluedroidClassic::end()
{
  spp_.end();
  inquiry_.end();
}

void EspBluedroidClassic::update()
{
  inquiry_.update();
  spp_.update();
}

EspBleBluedroid::EspBleBluedroid()
    : advertising_(this), scanner_(this), classic_(this)
{
}

EspBleBluedroid::~EspBleBluedroid()
{
  end();
}

bool EspBleBluedroid::begin(const EspBleConfig &config)
{
  const char *deviceName = config.deviceName == nullptr ? "" : config.deviceName;
  if (initialized_)
  {
    if (activeDeviceName_ != deviceName ||
        activePreferredMtu_ != config.preferredMtu ||
        !sameSecurityConfig(activeSecurity_, config.security))
    {
      setError(
        EspBleError::InvalidState,
        "Bluetooth stack is already initialized with a different configuration");
      return false;
    }
    clearError();
    return true;
  }
  if (BLEDevice::getInitialized())
  {
    setError(
      EspBleError::InvalidState,
      "Arduino BLE stack was initialized outside this EspBleBluedroid instance");
    return false;
  }
  if (config.preferredMtu < 23 || config.preferredMtu > 517)
  {
    setError(
      EspBleError::InvalidArgument, "preferred MTU must be between 23 and 517");
    return false;
  }
  if (!config.security.enabled &&
      (config.security.mitm || config.security.staticPasskeyEnabled ||
       config.security.ioCapability != EspBleSecurityIoCapability::None))
  {
    setError(
      EspBleError::InvalidArgument,
      "enable BLE security before configuring MITM or a passkey");
    return false;
  }
  if (static_cast<uint8_t>(config.security.ioCapability) >
      static_cast<uint8_t>(EspBleSecurityIoCapability::DisplayYesNo))
  {
    setError(
      EspBleError::InvalidArgument,
      "unsupported BLE Security I/O capability");
    return false;
  }
  if (config.security.staticPasskeyEnabled &&
      config.security.staticPasskey > 999999)
  {
    setError(
      EspBleError::InvalidArgument,
      "static BLE passkey must be between 000000 and 999999");
    return false;
  }
  if (config.security.mitm &&
      config.security.ioCapability == EspBleSecurityIoCapability::None)
  {
    setError(
      EspBleError::InvalidArgument,
      "MITM requires an input or output I/O capability");
    return false;
  }
  if (!config.security.mitm &&
      (config.security.staticPasskeyEnabled ||
       config.security.ioCapability != EspBleSecurityIoCapability::None))
  {
    setError(
      EspBleError::InvalidArgument,
      "a static passkey and I/O capability require MITM");
    return false;
  }
  connectionImpl_ = new (std::nothrow) EspBleConnectionImpl();
  if (connectionImpl_ == nullptr)
  {
    setError(
      EspBleError::ResourceExhausted, "failed to allocate connection state");
    return false;
  }
  if (!BLEDevice::init(deviceName))
  {
    delete connectionImpl_;
    connectionImpl_ = nullptr;
    setError(EspBleError::BackendFailure, "BLEDevice::init failed");
    return false;
  }
  if (BLEDevice::setMTU(config.preferredMtu) != ESP_OK)
  {
    BLEDevice::deinit(false);
    delete connectionImpl_;
    connectionImpl_ = nullptr;
    setError(EspBleError::BackendFailure, "failed to set preferred MTU");
    return false;
  }
  if (!classic_.begin(deviceName))
  {
    BLEDevice::deinit(false);
    delete connectionImpl_;
    connectionImpl_ = nullptr;
    return false;
  }
  if (config.security.enabled)
  {
    connectionImpl_->securityBackend = new (std::nothrow) BLESecurity();
    if (connectionImpl_->securityBackend == nullptr)
    {
      classic_.end();
      BLEDevice::deinit(false);
      delete connectionImpl_;
      connectionImpl_ = nullptr;
      setError(
        EspBleError::ResourceExhausted,
        "failed to allocate BLE security state");
      return false;
    }
    uint8_t ioCapability = ESP_IO_CAP_NONE;
    if (config.security.ioCapability ==
        EspBleSecurityIoCapability::DisplayOnly)
    {
      ioCapability = ESP_IO_CAP_OUT;
    }
    else if (config.security.ioCapability ==
             EspBleSecurityIoCapability::KeyboardOnly)
    {
      ioCapability = ESP_IO_CAP_IN;
    }
    else if (config.security.ioCapability ==
             EspBleSecurityIoCapability::DisplayYesNo)
    {
      ioCapability = ESP_IO_CAP_IO;
    }
    BLESecurity::setCapability(ioCapability);
    {
      std::lock_guard<std::mutex> lock(connectionImpl_->passkeyMutex);
      connectionImpl_->staticPasskeyEnabled =
        config.security.staticPasskeyEnabled;
      connectionImpl_->staticPasskey = config.security.staticPasskey;
      connectionImpl_->passkeyProvided = false;
      connectionImpl_->numericComparisonConfirmed = false;
    }
    if (config.security.staticPasskeyEnabled)
    {
      BLESecurity::setPassKey(true, config.security.staticPasskey);
    }
    else if (
      config.security.ioCapability ==
      EspBleSecurityIoCapability::DisplayOnly)
    {
      BLESecurity::setPassKey(false);
      BLESecurity::regenPassKeyOnConnect(true);
    }
    BLESecurity::setAuthenticationMode(
      config.security.bonding, config.security.mitm, true);
    BLESecurity::setForceAuthentication(config.security.pairOnConnect);
    BLEDevice::setSecurityCallbacks(&connectionImpl_->securityCallbacks);
  }
  else
  {
    BLESecurity::setAuthenticationMode(false, false, false);
    BLESecurity::setForceAuthentication(false);
    BLEDevice::setSecurityCallbacks(nullptr);
  }

  activeDeviceName_ = deviceName;
  activePreferredMtu_ = config.preferredMtu;
  activeSecurity_ = config.security;
  initialized_ = true;
  clearError();
  return true;
}

void EspBleBluedroid::end()
{
  if (!initialized_)
  {
    return;
  }
  if (scanner_.isScanning())
  {
    BLEDevice::getScan()->stop();
  }
  if (advertising_.advertising_)
  {
    BLEDevice::getAdvertising()->stop();
    advertising_.advertising_ = false;
  }
  classic_.end();
  scanner_.flushPendingResults();
  if (connectionImpl_ != nullptr)
  {
    {
      std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
      connectionImpl_->ending = true;
    }
    while (true)
    {
      {
        std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
        if (!connectionImpl_->connecting && !connectionImpl_->gattOperating)
        {
          break;
        }
      }
      delay(1);
    }
    std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
    connectionImpl_->active = false;
    connectionImpl_->eventHead = 0;
    connectionImpl_->eventCount = 0;
  }
  BLEDevice::setSecurityCallbacks(nullptr);
  BLESecurity::setAuthenticationMode(false, false, false);
  BLESecurity::setForceAuthentication(false);
  BLEDevice::deinit(false);
  initialized_ = false;
  delete connectionImpl_;
  connectionImpl_ = nullptr;
}

void EspBleBluedroid::update()
{
  advertising_.update();
  expireGattOperation();
  // Dispatch connection completions before Scan Results. A connect() accepted
  // from a Scan callback can therefore never complete in that same update().
  dispatchConnectionEvents();
  scanner_.dispatchPendingResults();
  classic_.update();
}

bool EspBleBluedroid::initialized() const
{
  return initialized_;
}

EspBluedroidCapabilities EspBleBluedroid::capabilities() const
{
  EspBluedroidCapabilities result;
#if defined(CONFIG_BT_CLASSIC_ENABLED)
  result.classic = true;
  result.dualMode = true;
  result.classicInquiry = true;
#endif
#if defined(CONFIG_BT_SPP_ENABLED)
  result.classicSpp = true;
#endif
  return result;
}

EspBleAdvertising &EspBleBluedroid::advertising()
{
  return advertising_;
}

EspBleScanner &EspBleBluedroid::scanner()
{
  return scanner_;
}

EspBluedroidClassic &EspBleBluedroid::classic()
{
  return classic_;
}

#ifdef ESP_BLE_BLUEDROID_TESTING
bool EspBleBluedroid::setSecurityResponseTimeoutForTest(
  uint32_t timeoutMilliseconds)
{
  if (!initialized_ || connectionImpl_ == nullptr)
  {
    setError(EspBleError::InvalidState,
      "Bluetooth stack is not initialized");
    return false;
  }
  if (timeoutMilliseconds == 0)
  {
    setError(EspBleError::InvalidArgument,
      "Security timeout must be nonzero");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
    connectionImpl_->securityResponseTimeoutMilliseconds =
      timeoutMilliseconds;
  }
  clearError();
  return true;
}
#endif

bool EspBleBluedroid::connect(
  const EspBleScanResult &scanResult, uint32_t timeoutMilliseconds)
{
  if (!initialized_ || connectionImpl_ == nullptr)
  {
    setError(EspBleError::InvalidState, "BLE stack is not initialized");
    return false;
  }
  if (!isValidBleAddress(scanResult.address.c_str()) ||
      static_cast<uint8_t>(scanResult.addressType) >
        static_cast<uint8_t>(EspBleAddressType::RandomIdentity) ||
      timeoutMilliseconds == 0)
  {
    setError(
      EspBleError::InvalidArgument,
      "valid peer address, address type, and nonzero timeout are required");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
    if (connectionImpl_->connecting || connectionImpl_->active)
    {
      setError(
        EspBleError::InvalidState,
        "a connection attempt or active connection already exists");
      return false;
    }
    connectionImpl_->target = scanResult;
    connectionImpl_->timeoutMilliseconds = timeoutMilliseconds;
    connectionImpl_->connecting = true;
  }

  TaskHandle_t task = nullptr;
  const BaseType_t result = xTaskCreate(
    EspBleConnectionImpl::connectTaskEntry,
    "espblebd-connect", 6144, connectionImpl_, 1, &task);
  if (result != pdPASS)
  {
    std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
    connectionImpl_->connecting = false;
    setError(EspBleError::ResourceExhausted, "failed to create connection task");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
    if (connectionImpl_->connecting)
    {
      connectionImpl_->connectTask = task;
    }
  }
  clearError();
  return true;
}

bool EspBleBluedroid::connect(
  const char *address,
  EspBleAddressType addressType,
  uint32_t timeoutMilliseconds)
{
  EspBleScanResult target;
  target.address = address == nullptr ? "" : address;
  target.addressType = addressType;
  return connect(target, timeoutMilliseconds);
}

bool EspBleBluedroid::disconnect(EspBleConnectionId connectionId)
{
  if (!initialized_ || connectionImpl_ == nullptr)
  {
    setError(EspBleError::InvalidState, "BLE stack is not initialized");
    return false;
  }
  BLEClient *client = nullptr;
  {
    std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
    if (!connectionImpl_->active ||
        connectionImpl_->connection.id != connectionId)
    {
      setError(EspBleError::InvalidArgument, "connection ID was not found");
      return false;
    }
    client = connectionImpl_->client;
    connectionImpl_->securityInputCancelled = true;
  }
  if (client == nullptr || client->disconnect() != ESP_OK)
  {
    {
      std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
      if (connectionImpl_->active &&
          connectionImpl_->connection.id == connectionId)
      {
        connectionImpl_->securityInputCancelled = false;
      }
    }
    setError(EspBleError::BackendFailure, "failed to request disconnection");
    return false;
  }
  clearError();
  return true;
}

bool EspBleBluedroid::discoverServices(
  EspBleConnectionId connectionId,
  uint32_t timeoutMilliseconds)
{
  return startGattOperation(
    EspBleGattOperation::DiscoverServices, connectionId, nullptr, nullptr,
    nullptr, 0, true, nullptr, timeoutMilliseconds);
}

size_t EspBleBluedroid::discoveredServiceCount(
  EspBleConnectionId connectionId) const
{
  if (connectionImpl_ == nullptr) return 0;
  std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
  const EspBleConnectionImpl::GattDatabaseSnapshot *database =
    connectionImpl_->gattDatabase;
  return database != nullptr && database->connectionId == connectionId
    ? database->serviceCount : 0;
}

bool EspBleBluedroid::discoveredService(
  EspBleConnectionId connectionId,
  size_t index,
  EspBleGattServiceInfo &service) const
{
  if (connectionImpl_ == nullptr) return false;
  std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
  const EspBleConnectionImpl::GattDatabaseSnapshot *database =
    connectionImpl_->gattDatabase;
  if (database == nullptr || database->connectionId != connectionId ||
      index >= database->serviceCount)
  {
    return false;
  }
  service = database->services[index];
  return true;
}

size_t EspBleBluedroid::discoveredCharacteristicCount(
  EspBleConnectionId connectionId,
  const char *serviceUuid) const
{
  if (connectionImpl_ == nullptr) return 0;
  std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
  const EspBleConnectionImpl::GattDatabaseSnapshot *database =
    connectionImpl_->gattDatabase;
  if (database == nullptr || database->connectionId != connectionId) return 0;
  size_t count = 0;
  for (size_t index = 0; index < database->characteristicCount; ++index)
  {
    if (serviceUuid == nullptr ||
        uuidEquals(database->characteristics[index].serviceUuid, serviceUuid))
    {
      ++count;
    }
  }
  return count;
}

bool EspBleBluedroid::discoveredCharacteristic(
  EspBleConnectionId connectionId,
  size_t index,
  EspBleGattCharacteristicInfo &characteristic,
  const char *serviceUuid) const
{
  if (connectionImpl_ == nullptr) return false;
  std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
  const EspBleConnectionImpl::GattDatabaseSnapshot *database =
    connectionImpl_->gattDatabase;
  if (database == nullptr || database->connectionId != connectionId) return false;
  size_t match = 0;
  for (size_t item = 0; item < database->characteristicCount; ++item)
  {
    if (serviceUuid != nullptr &&
        !uuidEquals(database->characteristics[item].serviceUuid, serviceUuid))
    {
      continue;
    }
    if (match++ == index)
    {
      characteristic = database->characteristics[item];
      return true;
    }
  }
  return false;
}

size_t EspBleBluedroid::discoveredDescriptorCount(
  EspBleConnectionId connectionId,
  const char *serviceUuid,
  const char *characteristicUuid) const
{
  if (connectionImpl_ == nullptr) return 0;
  std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
  const EspBleConnectionImpl::GattDatabaseSnapshot *database =
    connectionImpl_->gattDatabase;
  if (database == nullptr || database->connectionId != connectionId) return 0;
  size_t count = 0;
  for (size_t index = 0; index < database->descriptorCount; ++index)
  {
    const EspBleGattDescriptorInfo &descriptor = database->descriptors[index];
    if ((serviceUuid == nullptr || uuidEquals(descriptor.serviceUuid, serviceUuid)) &&
        (characteristicUuid == nullptr ||
         uuidEquals(descriptor.characteristicUuid, characteristicUuid)))
    {
      ++count;
    }
  }
  return count;
}

bool EspBleBluedroid::discoveredDescriptor(
  EspBleConnectionId connectionId,
  size_t index,
  EspBleGattDescriptorInfo &descriptor,
  const char *serviceUuid,
  const char *characteristicUuid) const
{
  if (connectionImpl_ == nullptr) return false;
  std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
  const EspBleConnectionImpl::GattDatabaseSnapshot *database =
    connectionImpl_->gattDatabase;
  if (database == nullptr || database->connectionId != connectionId) return false;
  size_t match = 0;
  for (size_t item = 0; item < database->descriptorCount; ++item)
  {
    const EspBleGattDescriptorInfo &candidate = database->descriptors[item];
    if ((serviceUuid != nullptr &&
         !uuidEquals(candidate.serviceUuid, serviceUuid)) ||
        (characteristicUuid != nullptr &&
         !uuidEquals(candidate.characteristicUuid, characteristicUuid)))
    {
      continue;
    }
    if (match++ == index)
    {
      descriptor = candidate;
      return true;
    }
  }
  return false;
}

bool EspBleBluedroid::readCharacteristic(
  EspBleConnectionId connectionId,
  const char *serviceUuid,
  const char *characteristicUuid,
  uint32_t timeoutMilliseconds)
{
  return startGattOperation(
    EspBleGattOperation::Read, connectionId, serviceUuid, characteristicUuid,
    nullptr, 0, true, nullptr, timeoutMilliseconds);
}

bool EspBleBluedroid::writeCharacteristic(
  EspBleConnectionId connectionId,
  const char *serviceUuid,
  const char *characteristicUuid,
  const uint8_t *data,
  size_t length,
  bool response,
  uint32_t timeoutMilliseconds)
{
  return startGattOperation(
    EspBleGattOperation::Write, connectionId, serviceUuid, characteristicUuid,
    data, length, response, nullptr, timeoutMilliseconds);
}

bool EspBleBluedroid::readDescriptor(
  EspBleConnectionId connectionId,
  const char *serviceUuid,
  const char *characteristicUuid,
  const char *descriptorUuid,
  uint32_t timeoutMilliseconds)
{
  return startGattOperation(
    EspBleGattOperation::ReadDescriptor, connectionId, serviceUuid,
    characteristicUuid, nullptr, 0, true, descriptorUuid,
    timeoutMilliseconds);
}

bool EspBleBluedroid::writeDescriptor(
  EspBleConnectionId connectionId,
  const char *serviceUuid,
  const char *characteristicUuid,
  const char *descriptorUuid,
  const uint8_t *data,
  size_t length,
  bool response,
  uint32_t timeoutMilliseconds)
{
  return startGattOperation(
    EspBleGattOperation::WriteDescriptor, connectionId, serviceUuid,
    characteristicUuid, data, length, response, descriptorUuid,
    timeoutMilliseconds);
}

bool EspBleBluedroid::writeCharacteristic(
  EspBleConnectionId connectionId,
  const char *serviceUuid,
  const char *characteristicUuid,
  const String &value,
  bool response,
  uint32_t timeoutMilliseconds)
{
  return writeCharacteristic(
    connectionId, serviceUuid, characteristicUuid,
    reinterpret_cast<const uint8_t *>(value.c_str()), value.length(),
    response, timeoutMilliseconds);
}

bool EspBleBluedroid::readCharacteristic(
  EspBleConnectionId connectionId,
  uint16_t characteristicHandle,
  uint32_t timeoutMilliseconds)
{
  if (characteristicHandle == 0)
  {
    setError(EspBleError::InvalidArgument,
      "characteristic handle must be non-zero");
    return false;
  }
  return startGattOperation(
    EspBleGattOperation::Read, connectionId, nullptr, nullptr,
    nullptr, 0, true, nullptr, timeoutMilliseconds, characteristicHandle);
}

bool EspBleBluedroid::writeCharacteristic(
  EspBleConnectionId connectionId,
  uint16_t characteristicHandle,
  const uint8_t *data,
  size_t length,
  bool response,
  uint32_t timeoutMilliseconds)
{
  if (characteristicHandle == 0)
  {
    setError(EspBleError::InvalidArgument,
      "characteristic handle must be non-zero");
    return false;
  }
  return startGattOperation(
    EspBleGattOperation::Write, connectionId, nullptr, nullptr,
    data, length, response, nullptr, timeoutMilliseconds,
    characteristicHandle);
}

bool EspBleBluedroid::writeCharacteristic(
  EspBleConnectionId connectionId,
  uint16_t characteristicHandle,
  const String &value,
  bool response,
  uint32_t timeoutMilliseconds)
{
  return writeCharacteristic(
    connectionId, characteristicHandle,
    reinterpret_cast<const uint8_t *>(value.c_str()), value.length(),
    response, timeoutMilliseconds);
}

bool EspBleBluedroid::subscribe(
  EspBleConnectionId connectionId,
  uint16_t characteristicHandle,
  bool notifications,
  uint32_t timeoutMilliseconds)
{
  if (characteristicHandle == 0)
  {
    setError(EspBleError::InvalidArgument,
      "characteristic handle must be non-zero");
    return false;
  }
  return startGattOperation(
    EspBleGattOperation::Subscribe, connectionId, nullptr, nullptr,
    nullptr, 0, notifications, nullptr, timeoutMilliseconds,
    characteristicHandle);
}

bool EspBleBluedroid::unsubscribe(
  EspBleConnectionId connectionId,
  uint16_t characteristicHandle,
  uint32_t timeoutMilliseconds)
{
  if (characteristicHandle == 0)
  {
    setError(EspBleError::InvalidArgument,
      "characteristic handle must be non-zero");
    return false;
  }
  return startGattOperation(
    EspBleGattOperation::Unsubscribe, connectionId, nullptr, nullptr,
    nullptr, 0, true, nullptr, timeoutMilliseconds, characteristicHandle);
}

bool EspBleBluedroid::writeDescriptor(
  EspBleConnectionId connectionId,
  const char *serviceUuid,
  const char *characteristicUuid,
  const char *descriptorUuid,
  const String &value,
  bool response,
  uint32_t timeoutMilliseconds)
{
  return writeDescriptor(
    connectionId, serviceUuid, characteristicUuid, descriptorUuid,
    reinterpret_cast<const uint8_t *>(value.c_str()), value.length(),
    response, timeoutMilliseconds);
}

bool EspBleBluedroid::subscribe(
  EspBleConnectionId connectionId,
  const char *serviceUuid,
  const char *characteristicUuid,
  bool notifications,
  uint32_t timeoutMilliseconds)
{
  return startGattOperation(
    EspBleGattOperation::Subscribe, connectionId, serviceUuid,
    characteristicUuid, nullptr, 0, notifications, nullptr,
    timeoutMilliseconds);
}

bool EspBleBluedroid::unsubscribe(
  EspBleConnectionId connectionId,
  const char *serviceUuid,
  const char *characteristicUuid,
  uint32_t timeoutMilliseconds)
{
  return startGattOperation(
    EspBleGattOperation::Unsubscribe, connectionId, serviceUuid,
    characteristicUuid, nullptr, 0, true, nullptr, timeoutMilliseconds);
}

bool EspBleBluedroid::startGattOperation(
  EspBleGattOperation operation,
  EspBleConnectionId connectionId,
  const char *serviceUuid,
  const char *characteristicUuid,
  const uint8_t *data,
  size_t length,
  bool response,
  const char *descriptorUuid,
  uint32_t timeoutMilliseconds,
  uint16_t characteristicHandle)
{
  if (!initialized_ || connectionImpl_ == nullptr)
  {
    setError(EspBleError::InvalidState, "BLE stack is not initialized");
    return false;
  }
  const bool databaseDiscovery =
    operation == EspBleGattOperation::DiscoverServices;
  const bool descriptorOperation =
    operation == EspBleGattOperation::ReadDescriptor ||
    operation == EspBleGattOperation::WriteDescriptor;
  const bool handleBased = characteristicHandle != 0;
  if ((!databaseDiscovery && !handleBased &&
       (serviceUuid == nullptr || serviceUuid[0] == '\0' ||
        characteristicUuid == nullptr || characteristicUuid[0] == '\0')) ||
      (descriptorOperation &&
       (descriptorUuid == nullptr || descriptorUuid[0] == '\0')) ||
      (data == nullptr && length != 0) || timeoutMilliseconds == 0 ||
      (operation != EspBleGattOperation::Read &&
       operation != EspBleGattOperation::Write &&
       operation != EspBleGattOperation::Subscribe &&
       operation != EspBleGattOperation::Unsubscribe &&
       operation != EspBleGattOperation::DiscoverServices &&
       operation != EspBleGattOperation::ReadDescriptor &&
       operation != EspBleGattOperation::WriteDescriptor))
  {
    setError(EspBleError::InvalidArgument, "invalid GATT operation arguments");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
    if (!connectionImpl_->active ||
        connectionImpl_->connection.id != connectionId)
    {
      setError(EspBleError::InvalidArgument, "Central connection ID was not found");
      return false;
    }
    if (connectionImpl_->gattOperating)
    {
      setError(EspBleError::InvalidState, "a GATT operation is already in progress");
      return false;
    }
    connectionImpl_->gattOperation = operation;
    connectionImpl_->gattConnectionId = connectionId;
    connectionImpl_->gattServiceUuid = serviceUuid == nullptr ? "" : serviceUuid;
    connectionImpl_->gattCharacteristicUuid =
      characteristicUuid == nullptr ? "" : characteristicUuid;
    connectionImpl_->gattDescriptorUuid =
      descriptorUuid == nullptr ? "" : descriptorUuid;
    connectionImpl_->gattCharacteristicHandle = characteristicHandle;
    if (databaseDiscovery)
    {
      delete connectionImpl_->gattDatabase;
      connectionImpl_->gattDatabase = nullptr;
    }
    connectionImpl_->gattWriteValue = length == 0
      ? String() : String(reinterpret_cast<const char *>(data), length);
    connectionImpl_->gattWriteResponse = response;
    connectionImpl_->gattStartedAt = millis();
    connectionImpl_->gattTimeoutMilliseconds = timeoutMilliseconds;
    connectionImpl_->gattTimedOut = false;
    connectionImpl_->gattOperating = true;
  }

  TaskHandle_t task = nullptr;
  const BaseType_t taskResult = xTaskCreate(
    EspBleConnectionImpl::gattTaskEntry,
    "espblebd-gatt", 6144, connectionImpl_, 1, &task);
  if (taskResult != pdPASS)
  {
    std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
    connectionImpl_->gattOperating = false;
    setError(EspBleError::ResourceExhausted, "failed to create GATT operation task");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
    if (connectionImpl_->gattOperating)
    {
      connectionImpl_->gattTask = task;
    }
  }
  clearError();
  return true;
}

size_t EspBleBluedroid::connectionCount() const
{
  if (connectionImpl_ == nullptr) return 0;
  std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
  return connectionImpl_->active ? 1 : 0;
}

bool EspBleBluedroid::connection(
  EspBleConnectionId connectionId, EspBleConnection &connection) const
{
  if (connectionImpl_ == nullptr) return false;
  std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
  if (!connectionImpl_->active ||
      connectionImpl_->connection.id != connectionId)
  {
    return false;
  }
  connection = connectionImpl_->connection;
  return true;
}

bool EspBleBluedroid::requestSecurity(EspBleConnectionId connectionId)
{
  if (!initialized_ || connectionImpl_ == nullptr ||
      !activeSecurity_.enabled)
  {
    setError(EspBleError::InvalidState,
      "BLE security is not enabled");
    return false;
  }
  BLEClient *client = nullptr;
  {
    std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
    if (!connectionImpl_->active ||
        connectionImpl_->connection.id != connectionId)
    {
      setError(EspBleError::InvalidArgument,
        "Central connection ID was not found");
      return false;
    }
    client = connectionImpl_->client;
  }
  int backendCode = ESP_FAIL;
  if (client == nullptr ||
      !BLESecurity::startSecurity(
        client->getPeerAddress().getNative(), &backendCode))
  {
    setError(EspBleError::BackendFailure,
      "failed to start BLE security");
    return false;
  }
  clearError();
  return true;
}

bool EspBleBluedroid::providePasskey(uint32_t passkey)
{
  if (!initialized_ || connectionImpl_ == nullptr)
  {
    setError(EspBleError::InvalidState,
      "Bluetooth stack is not initialized");
    return false;
  }
  if (passkey > 999999)
  {
    setError(EspBleError::InvalidArgument,
      "BLE passkey must be between 000000 and 999999");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(connectionImpl_->passkeyMutex);
    connectionImpl_->providedPasskey = passkey;
    connectionImpl_->passkeyProvided = true;
  }
  clearError();
  return true;
}

bool EspBleBluedroid::confirmNumericComparison(bool accept)
{
  if (!initialized_ || connectionImpl_ == nullptr)
  {
    setError(EspBleError::InvalidState,
      "Bluetooth stack is not initialized");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(connectionImpl_->passkeyMutex);
    connectionImpl_->numericComparisonAccept = accept;
    connectionImpl_->numericComparisonConfirmed = true;
  }
  clearError();
  return true;
}

size_t EspBleBluedroid::bondCount() const
{
  if (!initialized_) return 0;
  const int count = esp_ble_get_bond_device_num();
  return count > 0 ? static_cast<size_t>(count) : 0;
}

bool EspBleBluedroid::bond(size_t index, EspBleBond &bond) const
{
  if (!initialized_) return false;
  const int count = esp_ble_get_bond_device_num();
  if (count <= 0 || index >= static_cast<size_t>(count)) return false;
  esp_ble_bond_dev_t *bonds =
    new (std::nothrow) esp_ble_bond_dev_t[count];
  if (bonds == nullptr) return false;
  int listed = count;
  const bool success =
    esp_ble_get_bond_device_list(&listed, bonds) == ESP_OK &&
    index < static_cast<size_t>(listed);
  if (success)
  {
    bond.peerAddress = BLEAddress(bonds[index].bd_addr).toString();
    bond.peerAddressType =
      static_cast<EspBleAddressType>(bonds[index].bd_addr_type);
  }
  delete[] bonds;
  return success;
}

bool EspBleBluedroid::deleteBond(const EspBleBond &bond)
{
  if (!initialized_ || connectionImpl_ == nullptr)
  {
    setError(EspBleError::InvalidState,
      "BLE stack is not initialized");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
    if (connectionImpl_->active || connectionImpl_->connecting)
    {
      setError(EspBleError::InvalidState,
        "disconnect before deleting a BLE bond");
      return false;
    }
  }
  const int count = esp_ble_get_bond_device_num();
  esp_ble_bond_dev_t *bonds = count > 0
    ? new (std::nothrow) esp_ble_bond_dev_t[count] : nullptr;
  if (count > 0 && bonds == nullptr)
  {
    setError(EspBleError::ResourceExhausted,
      "failed to allocate BLE bond list");
    return false;
  }
  int listed = count;
  if (count > 0 &&
      esp_ble_get_bond_device_list(&listed, bonds) != ESP_OK)
  {
    delete[] bonds;
    setError(EspBleError::BackendFailure,
      "failed to enumerate BLE bonds");
    return false;
  }
  for (int index = 0; index < listed; ++index)
  {
    if (BLEAddress(bonds[index].bd_addr).toString().equalsIgnoreCase(
          bond.peerAddress) &&
        static_cast<uint8_t>(bond.peerAddressType) ==
          static_cast<uint8_t>(bonds[index].bd_addr_type))
    {
      const esp_err_t result =
        esp_ble_remove_bond_device(bonds[index].bd_addr);
      delete[] bonds;
      if (result != ESP_OK)
      {
        setError(EspBleError::BackendFailure,
          "failed to delete BLE bond");
        return false;
      }
      const uint32_t startedAt = millis();
      while (esp_ble_get_bond_device_num() >= count &&
             static_cast<uint32_t>(millis() - startedAt) < 2000)
      {
        delay(10);
      }
      if (esp_ble_get_bond_device_num() >= count)
      {
        setError(EspBleError::Timeout,
          "timed out waiting for BLE bond deletion");
        return false;
      }
      clearError();
      return true;
    }
  }
  delete[] bonds;
  setError(EspBleError::NotFound, "BLE bond was not found");
  return false;
}

bool EspBleBluedroid::deleteAllBonds()
{
  if (!initialized_ || connectionImpl_ == nullptr)
  {
    setError(EspBleError::InvalidState,
      "BLE stack is not initialized");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
    if (connectionImpl_->active || connectionImpl_->connecting)
    {
      setError(EspBleError::InvalidState,
        "disconnect before deleting BLE bonds");
      return false;
    }
  }
  const int count = esp_ble_get_bond_device_num();
  if (count <= 0)
  {
    clearError();
    return true;
  }
  esp_ble_bond_dev_t *bonds =
    new (std::nothrow) esp_ble_bond_dev_t[count];
  if (bonds == nullptr)
  {
    setError(EspBleError::ResourceExhausted,
      "failed to allocate BLE bond list");
    return false;
  }
  int listed = count;
  if (esp_ble_get_bond_device_list(&listed, bonds) != ESP_OK)
  {
    delete[] bonds;
    setError(EspBleError::BackendFailure,
      "failed to enumerate BLE bonds");
    return false;
  }
  for (int index = 0; index < listed; ++index)
  {
    if (esp_ble_remove_bond_device(bonds[index].bd_addr) != ESP_OK)
    {
      delete[] bonds;
      setError(EspBleError::BackendFailure,
        "failed to delete all BLE bonds");
      return false;
    }
  }
  delete[] bonds;
  const uint32_t startedAt = millis();
  while (esp_ble_get_bond_device_num() != 0 &&
         static_cast<uint32_t>(millis() - startedAt) < 2000)
  {
    delay(10);
  }
  if (esp_ble_get_bond_device_num() != 0)
  {
    setError(EspBleError::Timeout,
      "timed out waiting for BLE bond deletion");
    return false;
  }
  clearError();
  return true;
}

size_t EspBleBluedroid::droppedEventCount() const
{
  if (connectionImpl_ == nullptr) return 0;
  std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
  return connectionImpl_->droppedEvents;
}

void EspBleBluedroid::onConnected(ConnectionCallback callback)
{
  connectedCallback_ = std::move(callback);
}

void EspBleBluedroid::onDisconnected(ConnectionCallback callback)
{
  disconnectedCallback_ = std::move(callback);
}

void EspBleBluedroid::onConnectionFailed(ConnectionFailureCallback callback)
{
  connectionFailedCallback_ = std::move(callback);
}

void EspBleBluedroid::onSecurityChanged(SecurityChangedCallback callback)
{
  securityChangedCallback_ = std::move(callback);
}

void EspBleBluedroid::onPasskeyDisplayed(PasskeyDisplayedCallback callback)
{
  passkeyDisplayedCallback_ = std::move(callback);
}

void EspBleBluedroid::onNumericComparison(PasskeyDisplayedCallback callback)
{
  numericComparisonCallback_ = std::move(callback);
}

void EspBleBluedroid::onCharacteristicRead(GattResultCallback callback)
{
  characteristicReadCallback_ = std::move(callback);
}

void EspBleBluedroid::onCharacteristicWritten(GattResultCallback callback)
{
  characteristicWrittenCallback_ = std::move(callback);
}

void EspBleBluedroid::onDescriptorRead(GattResultCallback callback)
{
  descriptorReadCallback_ = std::move(callback);
}

void EspBleBluedroid::onDescriptorWritten(GattResultCallback callback)
{
  descriptorWrittenCallback_ = std::move(callback);
}

void EspBleBluedroid::onSubscribed(GattResultCallback callback)
{
  subscribedCallback_ = std::move(callback);
}

void EspBleBluedroid::onUnsubscribed(GattResultCallback callback)
{
  unsubscribedCallback_ = std::move(callback);
}

void EspBleBluedroid::onNotification(
  std::function<void(const EspBleGattNotification &notification)> callback)
{
  notificationCallback_ = std::move(callback);
}

void EspBleBluedroid::onServicesDiscovered(GattResultCallback callback)
{
  servicesDiscoveredCallback_ = std::move(callback);
}

void EspBleBluedroid::expireGattOperation()
{
  if (connectionImpl_ == nullptr) return;
  std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
  if (!connectionImpl_->gattOperating || connectionImpl_->gattTimedOut ||
      static_cast<uint32_t>(millis() - connectionImpl_->gattStartedAt) <
        connectionImpl_->gattTimeoutMilliseconds)
  {
    return;
  }

  connectionImpl_->gattTimedOut = true;
  EspBleConnectionImpl::Event event;
  event.type = EspBleConnectionImpl::EventType::GattResult;
  event.gattResult.operation = connectionImpl_->gattOperation;
  event.gattResult.connectionId = connectionImpl_->gattConnectionId;
  event.gattResult.serviceUuid = connectionImpl_->gattServiceUuid;
  event.gattResult.characteristicUuid = connectionImpl_->gattCharacteristicUuid;
  event.gattResult.descriptorUuid = connectionImpl_->gattDescriptorUuid;
  event.gattResult.handle = connectionImpl_->gattCharacteristicHandle;
  event.gattResult.response = connectionImpl_->gattWriteResponse;
  event.gattResult.error = EspBleError::Timeout;
  event.gattResult.detail = "GATT operation timed out";
  connectionImpl_->pushEventLocked(event);
}

void EspBleBluedroid::dispatchConnectionEvents()
{
  if (connectionImpl_ == nullptr) return;
  size_t eventsToDispatch;
  {
    std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
    eventsToDispatch = connectionImpl_->eventCount;
  }
  while (eventsToDispatch-- > 0)
  {
    EspBleConnectionImpl::Event event;
    {
      std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
      if (connectionImpl_->eventCount == 0) break;
      event = std::move(
        connectionImpl_->events[connectionImpl_->eventHead]);
      connectionImpl_->eventHead =
        (connectionImpl_->eventHead + 1) % EspBleConnectionImpl::EventCapacity;
      --connectionImpl_->eventCount;
    }
    if (event.type == EspBleConnectionImpl::EventType::Connected &&
        connectedCallback_)
    {
      connectedCallback_(event.connection);
    }
    else if (event.type == EspBleConnectionImpl::EventType::Disconnected &&
             disconnectedCallback_)
    {
      disconnectedCallback_(event.connection);
    }
    else if (event.type == EspBleConnectionImpl::EventType::Failed &&
             connectionFailedCallback_)
    {
      connectionFailedCallback_(event.failure);
    }
    else if (
      event.type == EspBleConnectionImpl::EventType::SecurityChanged &&
      securityChangedCallback_)
    {
      securityChangedCallback_(event.securityChanged);
    }
    else if (
      event.type == EspBleConnectionImpl::EventType::PasskeyDisplayed &&
      passkeyDisplayedCallback_)
    {
      passkeyDisplayedCallback_(event.passkeyDisplayed);
    }
    else if (
      event.type == EspBleConnectionImpl::EventType::NumericComparison &&
      numericComparisonCallback_)
    {
      numericComparisonCallback_(event.passkeyDisplayed);
    }
    else if (
      event.type == EspBleConnectionImpl::EventType::GattResult &&
      event.gattResult.operation == EspBleGattOperation::Read &&
      characteristicReadCallback_)
    {
      characteristicReadCallback_(event.gattResult);
    }
    else if (
      event.type == EspBleConnectionImpl::EventType::GattResult &&
      event.gattResult.operation == EspBleGattOperation::Write &&
      characteristicWrittenCallback_)
    {
      characteristicWrittenCallback_(event.gattResult);
    }
    else if (
      event.type == EspBleConnectionImpl::EventType::GattResult &&
      event.gattResult.operation == EspBleGattOperation::ReadDescriptor &&
      descriptorReadCallback_)
    {
      descriptorReadCallback_(event.gattResult);
    }
    else if (
      event.type == EspBleConnectionImpl::EventType::GattResult &&
      event.gattResult.operation == EspBleGattOperation::WriteDescriptor &&
      descriptorWrittenCallback_)
    {
      descriptorWrittenCallback_(event.gattResult);
    }
    else if (
      event.type == EspBleConnectionImpl::EventType::GattResult &&
      event.gattResult.operation == EspBleGattOperation::Subscribe &&
      subscribedCallback_)
    {
      subscribedCallback_(event.gattResult);
    }
    else if (
      event.type == EspBleConnectionImpl::EventType::GattResult &&
      event.gattResult.operation == EspBleGattOperation::Unsubscribe &&
      unsubscribedCallback_)
    {
      unsubscribedCallback_(event.gattResult);
    }
    else if (
      event.type == EspBleConnectionImpl::EventType::GattResult &&
      event.gattResult.operation == EspBleGattOperation::DiscoverServices &&
      servicesDiscoveredCallback_)
    {
      servicesDiscoveredCallback_(event.gattResult);
    }
    else if (
      event.type == EspBleConnectionImpl::EventType::Notification &&
      notificationCallback_)
    {
      notificationCallback_(event.notification);
    }
  }
}

EspBleError EspBleBluedroid::lastError() const
{
  return lastError_;
}

const char *EspBleBluedroid::lastErrorName() const
{
  switch (lastError_)
  {
  case EspBleError::None: return "None";
  case EspBleError::InvalidState: return "InvalidState";
  case EspBleError::InvalidArgument: return "InvalidArgument";
  case EspBleError::BackendFailure: return "BackendFailure";
  case EspBleError::ResourceExhausted: return "ResourceExhausted";
  case EspBleError::NotFound: return "NotFound";
  case EspBleError::Timeout: return "Timeout";
  case EspBleError::Unsupported: return "Unsupported";
  }
  return "Unknown";
}

const String &EspBleBluedroid::lastErrorDetail() const
{
  return lastErrorDetail_;
}

void EspBleBluedroid::clearError()
{
  lastError_ = EspBleError::None;
  lastErrorDetail_ = "";
}

void EspBleBluedroid::setError(EspBleError error, const char *detail)
{
  lastError_ = error;
  lastErrorDetail_ = detail == nullptr ? "" : detail;
}
