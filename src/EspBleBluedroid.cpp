#include "EspBleBluedroid.h"

#include <BLEAdvertising.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
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
  if (!BLEDevice::init(deviceName))
  {
    setError(EspBleError::BackendFailure, "BLEDevice::init failed");
    return false;
  }
  if (BLEDevice::setMTU(config.preferredMtu) != ESP_OK)
  {
    BLEDevice::deinit(false);
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
  BLEDevice::deinit(false);
  initialized_ = false;
}

void EspBleBluedroid::update()
{
  advertising_.update();
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
