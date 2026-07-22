#include "EspBleBluedroid.h"

#include <BLEAdvertising.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLERemoteCharacteristic.h>
#include <BLERemoteService.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <cctype>
#include <mutex>
#include <new>
#include <utility>

namespace
{
constexpr size_t ScanQueueCapacity = 16;
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

      std::lock_guard<std::mutex> lock(owner_->mutex);
      if (owner_->count == ScanQueueCapacity)
      {
        ++owner_->dropped;
        return;
      }
      const size_t tail = (owner_->head + owner_->count) % ScanQueueCapacity;
      owner_->queue[tail] = std::move(result);
      ++owner_->count;
    }

  private:
    EspBleScannerImpl *owner_;
  };

  EspBleScannerImpl() : callbacks(this) {}

  mutable std::mutex mutex;
  EspBleScanResult queue[ScanQueueCapacity];
  size_t head = 0;
  size_t count = 0;
  size_t dropped = 0;
  BackendCallbacks callbacks;
};

struct EspBleConnectionImpl
{
  static constexpr size_t EventCapacity = 8;
  static constexpr uint32_t ConnectWaitSliceMilliseconds = 1000;

  enum class EventType : uint8_t
  {
    Connected,
    Disconnected,
    Failed,
    GattResult,
  };
  struct Event
  {
    EventType type = EventType::Connected;
    EspBleConnection connection;
    EspBleConnectionFailure failure;
    EspBleGattResult gattResult;
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

  EspBleConnectionImpl() : callbacks(this) {}

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

  void backendConnected(BLEClient *connectedClient)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (ending || active)
    {
      return;
    }
    active = true;
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
    if (!ending)
    {
      pushEventLocked(event);
    }
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
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      result.operation = impl->gattOperation;
      result.connectionId = impl->gattConnectionId;
      result.serviceUuid = impl->gattServiceUuid;
      result.characteristicUuid = impl->gattCharacteristicUuid;
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
        BLERemoteCharacteristic *characteristic =
          service->getCharacteristic(result.characteristicUuid.c_str());
        if (characteristic == nullptr)
        {
          result.error = EspBleError::NotFound;
          result.detail = "GATT characteristic was not found";
        }
        else
        {
          result.handle = characteristic->getHandle();
          result.readable = characteristic->canRead();
          result.writable = characteristic->canWrite();
          result.writableWithoutResponse = characteristic->canWriteNoResponse();
          result.notifiable = characteristic->canNotify();
          result.indicatable = characteristic->canIndicate();
          if (result.operation == EspBleGattOperation::Read && !result.readable)
          {
            result.error = EspBleError::InvalidState;
            result.detail = "GATT characteristic is not readable";
          }
          else if (result.operation == EspBleGattOperation::Read)
          {
            result.value = characteristic->readValue();
            result.success = true;
          }
          else
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
        }
      }
    }

    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      if (!impl->ending && !impl->gattTimedOut)
      {
        Event event;
        event.type = EventType::GattResult;
        event.gattResult = result;
        impl->pushEventLocked(event);
      }
      impl->gattOperating = false;
      impl->gattTask = nullptr;
    }
    vTaskDelete(nullptr);
  }

  mutable std::mutex mutex;
  BLEClient *client = nullptr;
  ClientCallbacks callbacks;
  bool connecting = false;
  bool ending = false;
  bool active = false;
  TaskHandle_t connectTask = nullptr;
  bool gattOperating = false;
  TaskHandle_t gattTask = nullptr;
  EspBleConnectionId gattConnectionId = 0;
  EspBleGattOperation gattOperation = EspBleGattOperation::Read;
  String gattServiceUuid;
  String gattCharacteristicUuid;
  String gattWriteValue;
  bool gattWriteResponse = true;
  uint32_t gattStartedAt = 0;
  uint32_t gattTimeoutMilliseconds = 10000;
  bool gattTimedOut = false;
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

EspBleBluedroid::EspBleBluedroid()
    : advertising_(this), scanner_(this)
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
  if (config.security.enabled)
  {
    setError(
      EspBleError::Unsupported,
      "BLE security is not implemented in EspBleBluedroid yet");
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
}

bool EspBleBluedroid::initialized() const
{
  return initialized_;
}

EspBleAdvertising &EspBleBluedroid::advertising()
{
  return advertising_;
}

EspBleScanner &EspBleBluedroid::scanner()
{
  return scanner_;
}

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
  }
  if (client == nullptr || client->disconnect() != ESP_OK)
  {
    setError(EspBleError::BackendFailure, "failed to request disconnection");
    return false;
  }
  clearError();
  return true;
}

bool EspBleBluedroid::readCharacteristic(
  EspBleConnectionId connectionId,
  const char *serviceUuid,
  const char *characteristicUuid,
  uint32_t timeoutMilliseconds)
{
  return startGattOperation(
    EspBleGattOperation::Read, connectionId, serviceUuid, characteristicUuid,
    nullptr, 0, true, timeoutMilliseconds);
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
    data, length, response, timeoutMilliseconds);
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

bool EspBleBluedroid::startGattOperation(
  EspBleGattOperation operation,
  EspBleConnectionId connectionId,
  const char *serviceUuid,
  const char *characteristicUuid,
  const uint8_t *data,
  size_t length,
  bool response,
  uint32_t timeoutMilliseconds)
{
  if (!initialized_ || connectionImpl_ == nullptr)
  {
    setError(EspBleError::InvalidState, "BLE stack is not initialized");
    return false;
  }
  if (serviceUuid == nullptr || serviceUuid[0] == '\0' ||
      characteristicUuid == nullptr || characteristicUuid[0] == '\0' ||
      (data == nullptr && length != 0) || timeoutMilliseconds == 0 ||
      (operation != EspBleGattOperation::Read &&
       operation != EspBleGattOperation::Write))
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
    connectionImpl_->gattServiceUuid = serviceUuid;
    connectionImpl_->gattCharacteristicUuid = characteristicUuid;
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

void EspBleBluedroid::onCharacteristicRead(GattResultCallback callback)
{
  characteristicReadCallback_ = std::move(callback);
}

void EspBleBluedroid::onCharacteristicWritten(GattResultCallback callback)
{
  characteristicWrittenCallback_ = std::move(callback);
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
