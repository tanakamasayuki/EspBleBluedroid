#include "EspBleBluedroid.h"
#include "internal/EspBleBluedroidCodec.h"

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
#include <cstring>
#include <mutex>
#include <new>
#include <set>
#include <string>
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
constexpr size_t ClassicSecurityEventQueueCapacity = 8;
constexpr size_t SppEventQueueCapacity = 8;

struct BleTxPowerLevel
{
  int8_t dBm;
  esp_power_level_t backend;
};

constexpr BleTxPowerLevel BleTxPowerLevels[] = {
  {-12, ESP_PWR_LVL_N12},
  {-9, ESP_PWR_LVL_N9},
  {-6, ESP_PWR_LVL_N6},
  {-3, ESP_PWR_LVL_N3},
  {0, ESP_PWR_LVL_N0},
  {3, ESP_PWR_LVL_P3},
  {6, ESP_PWR_LVL_P6},
  {9, ESP_PWR_LVL_P9},
};

int8_t bleTxPowerDbm(esp_power_level_t level)
{
  for (const BleTxPowerLevel &candidate : BleTxPowerLevels)
  {
    if (candidate.backend == level) return candidate.dBm;
  }
  return INT8_MIN;
}

bool uuidEquals(const String &left, const char *right)
{
  return espblebluedroid::internal::uuidEquals(left.c_str(), right);
}

String formatBackendUuid(const esp_bt_uuid_t &backend)
{
  espblebluedroid::internal::BleUuid uuid;
  if (backend.len == ESP_UUID_LEN_16)
  {
    uuid.bitSize = 16;
    memcpy(uuid.bytes.data(), &backend.uuid.uuid16, sizeof(uint16_t));
  }
  else if (backend.len == ESP_UUID_LEN_32)
  {
    uuid.bitSize = 32;
    memcpy(uuid.bytes.data(), &backend.uuid.uuid32, sizeof(uint32_t));
  }
  else if (backend.len == ESP_UUID_LEN_128)
  {
    uuid.bitSize = 128;
    memcpy(uuid.bytes.data(), backend.uuid.uuid128, 16);
  }
  const std::string formatted =
    espblebluedroid::internal::formatBleUuid(uuid);
  return String(formatted.c_str());
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

BLERemoteDescriptor *findDescriptorByHandle(
  BLEClient *client, uint16_t handle, String &serviceUuid,
  BLERemoteCharacteristic *&owner)
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
    for (const auto &characteristicItem : *characteristics)
    {
      BLERemoteCharacteristic *characteristic = characteristicItem.second;
      if (characteristic == nullptr) continue;
      std::map<std::string, BLERemoteDescriptor *> *descriptors =
        characteristic->getDescriptors();
      if (descriptors == nullptr) continue;
      for (const auto &descriptorItem : *descriptors)
      {
        BLERemoteDescriptor *descriptor = descriptorItem.second;
        if (descriptor != nullptr && descriptor->getHandle() == handle)
        {
          serviceUuid = service->getUUID().toString();
          owner = characteristic;
          return descriptor;
        }
      }
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

bool sameClassicSecurityConfig(
  const EspBluedroidClassicSecurityConfig &left,
  const EspBluedroidClassicSecurityConfig &right)
{
  return left.enabled == right.enabled &&
    left.ioCapability == right.ioCapability &&
    left.responseTimeoutMilliseconds ==
      right.responseTimeoutMilliseconds;
}

bool isValidBleAddress(const char *address)
{
  uint8_t parsed[6] = {};
  return espblebluedroid::internal::parseBleAddress(address, parsed);
}
} // namespace

struct EspBleScannerImpl
{
  struct QueueEntry
  {
    EspBleScanResult result;
    uint32_t readyAtMs = 0;
  };

  static void mergeResult(
    EspBleScanResult &destination, const EspBleScanResult &source)
  {
    destination.rssi = source.rssi;
    destination.connectable =
      destination.connectable || source.connectable;
    destination.scannable = destination.scannable || source.scannable;
    if (!source.name.isEmpty()) destination.name = source.name;
    if (!source.manufacturerData.isEmpty())
    {
      destination.manufacturerData = source.manufacturerData;
    }
    if (source.appearance != 0) destination.appearance = source.appearance;
    if (source.txPowerLevelPresent)
    {
      destination.txPowerLevel = source.txPowerLevel;
      destination.txPowerLevelPresent = true;
    }

    for (size_t sourceIndex = 0;
         sourceIndex < source.serviceUuidCount;
         ++sourceIndex)
    {
      bool found = false;
      for (size_t destinationIndex = 0;
           destinationIndex < destination.serviceUuidCount;
           ++destinationIndex)
      {
        if (uuidEquals(
              destination.serviceUuids[destinationIndex],
              source.serviceUuids[sourceIndex].c_str()))
        {
          found = true;
          break;
        }
      }
      if (!found &&
          destination.serviceUuidCount < EspBleScanResult::MaxServiceUuids)
      {
        destination.serviceUuids[destination.serviceUuidCount++] =
          source.serviceUuids[sourceIndex];
      }
    }

    for (size_t sourceIndex = 0;
         sourceIndex < source.serviceDataCount;
         ++sourceIndex)
    {
      size_t destinationIndex = 0;
      for (; destinationIndex < destination.serviceDataCount;
           ++destinationIndex)
      {
        if (uuidEquals(
              destination.serviceData[destinationIndex].uuid,
              source.serviceData[sourceIndex].uuid.c_str()))
        {
          break;
        }
      }
      if (destinationIndex < destination.serviceDataCount)
      {
        destination.serviceData[destinationIndex] =
          source.serviceData[sourceIndex];
      }
      else if (
        destination.serviceDataCount < EspBleScanResult::MaxServiceData)
      {
        destination.serviceData[destination.serviceDataCount++] =
          source.serviceData[sourceIndex];
      }
    }
  }

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
      const int serviceDataCount = device.getServiceDataCount();
      for (int index = 0;
           index < serviceDataCount &&
             result.serviceDataCount < EspBleScanResult::MaxServiceData;
           ++index)
      {
        EspBleServiceData &block =
          result.serviceData[result.serviceDataCount++];
        block.uuid = device.getServiceDataUUID(index).toString();
        block.data = device.getServiceData(index);
      }
      if (device.haveAppearance())
      {
        result.appearance = device.getAppearance();
      }
      if (device.haveTXPower())
      {
        result.txPowerLevel = device.getTXPower();
        result.txPowerLevelPresent = true;
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

      owner_->enqueue(std::move(result), false);
    }

  private:
    EspBleScannerImpl *owner_;
  };

  EspBleScannerImpl() : callbacks(this) {}

  static void appendServiceUuid(
    EspBleScanResult &result,
    const uint8_t *data,
    size_t length)
  {
    if (result.serviceUuidCount >= EspBleScanResult::MaxServiceUuids) return;
    espblebluedroid::internal::BleUuid uuid;
    uuid.bitSize = static_cast<uint8_t>(length * 8);
    memcpy(uuid.bytes.data(), data, length);
    const std::string formatted =
      espblebluedroid::internal::formatBleUuid(uuid);
    if (!formatted.empty())
    {
      result.serviceUuids[result.serviceUuidCount++] = formatted.c_str();
    }
  }

  static void parseAdvertisingData(
    EspBleScanResult &result,
    const uint8_t *payload,
    size_t payloadLength)
  {
    size_t offset = 0;
    while (offset < payloadLength)
    {
      const size_t fieldLength = payload[offset];
      if (fieldLength == 0) break;
      if (fieldLength < 1 || offset + fieldLength + 1 > payloadLength) break;
      const uint8_t type = payload[offset + 1];
      const uint8_t *data = payload + offset + 2;
      const size_t length = fieldLength - 1;
      if ((type == ESP_BLE_AD_TYPE_NAME_CMPL ||
           type == ESP_BLE_AD_TYPE_NAME_SHORT) &&
          (result.name.isEmpty() || type == ESP_BLE_AD_TYPE_NAME_CMPL))
      {
        result.name = String(
          reinterpret_cast<const char *>(data), length);
      }
      else if (type == ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE)
      {
        result.manufacturerData = String(
          reinterpret_cast<const char *>(data), length);
      }
      else if (type == ESP_BLE_AD_TYPE_APPEARANCE && length == 2)
      {
        result.appearance = static_cast<uint16_t>(data[0]) |
          (static_cast<uint16_t>(data[1]) << 8);
      }
      else if (type == ESP_BLE_AD_TYPE_TX_PWR && length == 1)
      {
        result.txPowerLevel = static_cast<int8_t>(data[0]);
        result.txPowerLevelPresent = true;
      }
      else if (type == ESP_BLE_AD_TYPE_16SRV_CMPL ||
               type == ESP_BLE_AD_TYPE_16SRV_PART)
      {
        for (size_t index = 0; index + 2 <= length; index += 2)
        {
          appendServiceUuid(result, data + index, 2);
        }
      }
      else if (type == ESP_BLE_AD_TYPE_32SRV_CMPL ||
               type == ESP_BLE_AD_TYPE_32SRV_PART)
      {
        for (size_t index = 0; index + 4 <= length; index += 4)
        {
          appendServiceUuid(result, data + index, 4);
        }
      }
      else if (type == ESP_BLE_AD_TYPE_128SRV_CMPL ||
               type == ESP_BLE_AD_TYPE_128SRV_PART)
      {
        for (size_t index = 0; index + 16 <= length; index += 16)
        {
          appendServiceUuid(result, data + index, 16);
        }
      }
      else if (type == ESP_BLE_AD_TYPE_SERVICE_DATA ||
               type == ESP_BLE_AD_TYPE_32SERVICE_DATA ||
               type == ESP_BLE_AD_TYPE_128SERVICE_DATA)
      {
        const size_t uuidLength = type == ESP_BLE_AD_TYPE_SERVICE_DATA
          ? 2
          : (type == ESP_BLE_AD_TYPE_32SERVICE_DATA ? 4 : 16);
        if (length >= uuidLength &&
            result.serviceDataCount < EspBleScanResult::MaxServiceData)
        {
          espblebluedroid::internal::BleUuid uuid;
          uuid.bitSize = static_cast<uint8_t>(uuidLength * 8);
          memcpy(uuid.bytes.data(), data, uuidLength);
          const std::string formatted =
            espblebluedroid::internal::formatBleUuid(uuid);
          EspBleServiceData &block =
            result.serviceData[result.serviceDataCount++];
          block.uuid = formatted.c_str();
          block.data = String(
            reinterpret_cast<const char *>(data + uuidLength),
            length - uuidLength);
        }
      }
      offset += fieldLength + 1;
    }
  }

  void handleGapEvent(
    esp_gap_ble_cb_event_t event,
    esp_ble_gap_cb_param_t *param)
  {
    if (event == ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT)
    {
      scanParamsSucceeded.store(
        param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS,
        std::memory_order_release);
      scanParamsCompleted.store(true, std::memory_order_release);
      return;
    }
    if (event == ESP_GAP_BLE_SCAN_START_COMPLETE_EVT)
    {
      const bool success =
        param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS;
      scanning.store(success, std::memory_order_release);
      scanStartSucceeded.store(success, std::memory_order_release);
      scanStartCompleted.store(true, std::memory_order_release);
      return;
    }
    if (event == ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT)
    {
      scanning.store(false, std::memory_order_release);
      scanStopCompleted.store(true, std::memory_order_release);
      return;
    }
    if (event != ESP_GAP_BLE_SCAN_RESULT_EVT) return;
    if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT)
    {
      scanning.store(false, std::memory_order_release);
      return;
    }
    if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) return;

    EspBleScanResult result;
    const std::string address =
      espblebluedroid::internal::formatBleAddress(param->scan_rst.bda);
    result.address = address.c_str();
    result.addressType =
      static_cast<EspBleAddressType>(param->scan_rst.ble_addr_type);
    result.rssi = param->scan_rst.rssi;
    result.connectable =
      param->scan_rst.ble_evt_type == ESP_BLE_EVT_CONN_ADV ||
      param->scan_rst.ble_evt_type == ESP_BLE_EVT_CONN_DIR_ADV;
    result.scannable =
      param->scan_rst.ble_evt_type == ESP_BLE_EVT_CONN_ADV ||
      param->scan_rst.ble_evt_type == ESP_BLE_EVT_DISC_ADV;
    parseAdvertisingData(
      result,
      param->scan_rst.ble_adv,
      static_cast<size_t>(param->scan_rst.adv_data_len) +
        param->scan_rst.scan_rsp_len);
    enqueue(std::move(result), false);
  }

  bool enqueue(EspBleScanResult result, bool injected)
  {
    std::lock_guard<std::mutex> lock(mutex);
    const std::string address(result.address.c_str());
    if (!injected && !wantDuplicates && !address.empty() &&
        reportedAddresses.count(address) != 0)
    {
      return true;
    }
    if (!injected && active && !address.empty())
    {
      for (size_t offset = 0; offset < count; ++offset)
      {
        QueueEntry &entry = queue[(head + offset) % ScanQueueCapacity];
        if (entry.result.address.equalsIgnoreCase(result.address))
        {
          mergeResult(entry.result, result);
          return true;
        }
      }
    }
    if (count == ScanQueueCapacity)
    {
      ++dropped;
      return false;
    }
    const size_t tail = (head + count) % ScanQueueCapacity;
    queue[tail].result = std::move(result);
    queue[tail].readyAtMs =
      !injected && active ? millis() + ActiveScanMergeMilliseconds : 0;
    ++count;
    return true;
  }

  static constexpr uint32_t ActiveScanMergeMilliseconds = 30;
  mutable std::mutex mutex;
  QueueEntry queue[ScanQueueCapacity];
  size_t head = 0;
  size_t count = 0;
  size_t dropped = 0;
  bool active = false;
  bool wantDuplicates = false;
  std::set<std::string> reportedAddresses;
  BackendCallbacks callbacks;
  std::atomic<bool> scanParamsCompleted{false};
  std::atomic<bool> scanParamsSucceeded{false};
  std::atomic<bool> scanStartCompleted{false};
  std::atomic<bool> scanStartSucceeded{false};
  std::atomic<bool> scanStopCompleted{false};
  std::atomic<bool> scanning{false};
};

namespace
{
std::atomic<EspBleScannerImpl *> activeBleScanner{nullptr};
}

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

struct EspBluedroidClassicImpl
{
  enum class EventType : uint8_t
  {
    SecurityChanged,
    NumericComparison,
    PasskeyDisplayed,
    PasskeyRequested,
  };

  struct Event
  {
    EventType type = EventType::SecurityChanged;
    EspBluedroidClassicSecurityChanged securityChanged;
    EspBluedroidClassicNumericComparison numericComparison;
    EspBluedroidClassicPasskeyDisplayed passkeyDisplayed;
    EspBluedroidClassicPasskeyRequested passkeyRequested;
  };

  bool enqueue(Event event)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (eventCount == ClassicSecurityEventQueueCapacity)
    {
      ++dropped;
      return false;
    }
    const size_t tail =
      (eventHead + eventCount) % ClassicSecurityEventQueueCapacity;
    events[tail] = std::move(event);
    ++eventCount;
    return true;
  }

  mutable std::mutex mutex;
  Event events[ClassicSecurityEventQueueCapacity];
  size_t eventHead = 0;
  size_t eventCount = 0;
  size_t dropped = 0;
  EspBluedroidClassicSecurityConfig security;
  bool numericComparisonCallbackConfigured = false;
  bool numericComparisonPending = false;
  String numericComparisonAddress;
  esp_bd_addr_t numericComparisonBackendAddress = {};
  uint32_t numericComparisonDeadlineMs = 0;
  bool passkeyRequestedCallbackConfigured = false;
  bool passkeyPending = false;
  String passkeyAddress;
  esp_bd_addr_t passkeyBackendAddress = {};
  uint32_t passkeyDeadlineMs = 0;
};

#if defined(CONFIG_BT_CLASSIC_ENABLED)
namespace
{
std::atomic<EspBluedroidClassicInquiryImpl *> activeClassicInquiry{nullptr};
std::atomic<EspBluedroidClassicImpl *> activeClassic{nullptr};

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
  if (parameter == nullptr) return;
  EspBluedroidClassicInquiryImpl *inquiry =
    activeClassicInquiry.load(std::memory_order_acquire);

  if (inquiry != nullptr && event == ESP_BT_GAP_DISC_RES_EVT)
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
    inquiry->enqueue(std::move(result));
  }
  else if (
    inquiry != nullptr &&
    event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT &&
    parameter->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED)
  {
    std::lock_guard<std::mutex> lock(inquiry->mutex);
    inquiry->running = false;
    inquiry->completionCancelled = inquiry->stopRequested;
    inquiry->stopRequested = false;
    inquiry->completionPending = true;
  }

  EspBluedroidClassicImpl *classic =
    activeClassic.load(std::memory_order_acquire);
  if (classic == nullptr) return;
  if (event == ESP_BT_GAP_AUTH_CMPL_EVT)
  {
    {
      std::lock_guard<std::mutex> lock(classic->mutex);
      if (
        classic->numericComparisonPending &&
        memcmp(
          classic->numericComparisonBackendAddress,
          parameter->auth_cmpl.bda, ESP_BD_ADDR_LEN) == 0)
      {
        classic->numericComparisonPending = false;
        classic->numericComparisonAddress = "";
        memset(
          classic->numericComparisonBackendAddress, 0,
          sizeof(classic->numericComparisonBackendAddress));
        classic->numericComparisonDeadlineMs = 0;
      }
      if (
        classic->passkeyPending &&
        memcmp(
          classic->passkeyBackendAddress,
          parameter->auth_cmpl.bda, ESP_BD_ADDR_LEN) == 0)
      {
        classic->passkeyPending = false;
        classic->passkeyAddress = "";
        memset(
          classic->passkeyBackendAddress, 0,
          sizeof(classic->passkeyBackendAddress));
        classic->passkeyDeadlineMs = 0;
      }
    }
    EspBluedroidClassicImpl::Event queued;
    queued.type =
      EspBluedroidClassicImpl::EventType::SecurityChanged;
    queued.securityChanged.peerAddress =
      classicAddress(parameter->auth_cmpl.bda);
    queued.securityChanged.success =
      parameter->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS;
    queued.securityChanged.status = parameter->auth_cmpl.stat;
    classic->enqueue(std::move(queued));
  }
  else if (event == ESP_BT_GAP_CFM_REQ_EVT)
  {
    bool canRequest = false;
    bool justWorks = false;
    {
      std::lock_guard<std::mutex> lock(classic->mutex);
      justWorks =
        !classic->security.enabled ||
        classic->security.ioCapability ==
          EspBluedroidClassicSecurityIoCapability::None;
      canRequest =
        classic->security.enabled &&
        classic->security.ioCapability ==
          EspBluedroidClassicSecurityIoCapability::DisplayYesNo &&
        classic->numericComparisonCallbackConfigured &&
        !classic->numericComparisonPending;
      if (canRequest)
      {
        classic->numericComparisonPending = true;
        classic->numericComparisonAddress =
          classicAddress(parameter->cfm_req.bda);
        memcpy(
          classic->numericComparisonBackendAddress,
          parameter->cfm_req.bda, ESP_BD_ADDR_LEN);
        classic->numericComparisonDeadlineMs =
          millis() + classic->security.responseTimeoutMilliseconds;
      }
    }
    if (justWorks)
    {
      esp_bt_gap_ssp_confirm_reply(parameter->cfm_req.bda, true);
      return;
    }
    if (!canRequest)
    {
      esp_bt_gap_ssp_confirm_reply(parameter->cfm_req.bda, false);
      return;
    }
    EspBluedroidClassicImpl::Event queued;
    queued.type =
      EspBluedroidClassicImpl::EventType::NumericComparison;
    queued.numericComparison.peerAddress =
      classicAddress(parameter->cfm_req.bda);
    queued.numericComparison.value = parameter->cfm_req.num_val;
    if (!classic->enqueue(std::move(queued)))
    {
      {
        std::lock_guard<std::mutex> lock(classic->mutex);
        classic->numericComparisonPending = false;
        classic->numericComparisonAddress = "";
        memset(
          classic->numericComparisonBackendAddress, 0,
          sizeof(classic->numericComparisonBackendAddress));
        classic->numericComparisonDeadlineMs = 0;
      }
      esp_bt_gap_ssp_confirm_reply(parameter->cfm_req.bda, false);
    }
  }
  else if (event == ESP_BT_GAP_KEY_NOTIF_EVT)
  {
    EspBluedroidClassicImpl::Event queued;
    queued.type =
      EspBluedroidClassicImpl::EventType::PasskeyDisplayed;
    queued.passkeyDisplayed.peerAddress =
      classicAddress(parameter->key_notif.bda);
    queued.passkeyDisplayed.passkey = parameter->key_notif.passkey;
    classic->enqueue(std::move(queued));
  }
  else if (event == ESP_BT_GAP_KEY_REQ_EVT)
  {
    bool canRequest = false;
    {
      std::lock_guard<std::mutex> lock(classic->mutex);
      canRequest =
        classic->security.enabled &&
        classic->security.ioCapability ==
          EspBluedroidClassicSecurityIoCapability::KeyboardOnly &&
        classic->passkeyRequestedCallbackConfigured &&
        !classic->passkeyPending;
      if (canRequest)
      {
        classic->passkeyPending = true;
        classic->passkeyAddress =
          classicAddress(parameter->key_req.bda);
        memcpy(
          classic->passkeyBackendAddress,
          parameter->key_req.bda, ESP_BD_ADDR_LEN);
        classic->passkeyDeadlineMs =
          millis() + classic->security.responseTimeoutMilliseconds;
      }
    }
    if (!canRequest)
    {
      esp_bt_gap_ssp_passkey_reply(parameter->key_req.bda, false, 0);
      return;
    }
    EspBluedroidClassicImpl::Event queued;
    queued.type =
      EspBluedroidClassicImpl::EventType::PasskeyRequested;
    queued.passkeyRequested.peerAddress =
      classicAddress(parameter->key_req.bda);
    if (!classic->enqueue(std::move(queued)))
    {
      {
        std::lock_guard<std::mutex> lock(classic->mutex);
        classic->passkeyPending = false;
        classic->passkeyAddress = "";
        memset(
          classic->passkeyBackendAddress, 0,
          sizeof(classic->passkeyBackendAddress));
        classic->passkeyDeadlineMs = 0;
      }
      esp_bt_gap_ssp_passkey_reply(parameter->key_req.bda, false, 0);
    }
  }
  else if (event == ESP_BT_GAP_PIN_REQ_EVT)
  {
    esp_bt_pin_code_t pin = {};
    esp_bt_gap_pin_reply(parameter->pin_req.bda, false, 0, pin);
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
    WriteCompleted,
    ConnectionFailed,
  };

  struct Event
  {
    EventType type = EventType::ServerStarted;
    EspBluedroidSppSession session;
    EspBluedroidSppData data;
    EspBluedroidSppWriteResult writeResult;
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
  EspBluedroidSppSecurity serverSecurity =
    EspBluedroidSppSecurity::None;
  uint32_t backendHandle = 0;
  EspBluedroidSppSession activeSession;
  EspBluedroidSppSessionId nextSessionId = 1;
  String txQueue[EspBluedroidSpp::WriteQueueCapacity];
  size_t txHead = 0;
  size_t txCount = 0;
  size_t droppedWrites = 0;
  bool txInFlight = false;
  bool txCongested = false;
  std::atomic<bool> writeCompletionEventsEnabled{false};
  uint8_t rxBuffer[EspBluedroidSpp::ReceiveBufferCapacity] = {};
  size_t rxHead = 0;
  size_t rxCount = 0;
  size_t droppedReceiveBytes = 0;
  bool connecting = false;
  String connectAddress;
  esp_bd_addr_t connectBackendAddress = {};
  uint32_t connectDeadlineMs = 0;
  EspBluedroidSppSecurity connectSecurity =
    EspBluedroidSppSecurity::None;
};

#if defined(CONFIG_BT_SPP_ENABLED)
namespace
{
std::atomic<EspBluedroidSppImpl *> activeSpp{nullptr};

esp_spp_sec_t sppSecurityMask(EspBluedroidSppSecurity security)
{
  if (security == EspBluedroidSppSecurity::Authenticate)
  {
    return ESP_SPP_SEC_AUTHENTICATE;
  }
  if (security == EspBluedroidSppSecurity::AuthenticatedEncrypted)
  {
    return static_cast<esp_spp_sec_t>(
      ESP_SPP_SEC_AUTHENTICATE | ESP_SPP_SEC_ENCRYPT);
  }
  return ESP_SPP_SEC_NONE;
}

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
  const esp_err_t status = esp_spp_write(handle, length, data);
  if (status != ESP_OK)
  {
    EspBluedroidSppWriteResult result;
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      result.sessionId = impl->activeSession.id;
      result.length = length;
      result.error = EspBleError::BackendFailure;
      result.detail =
        String("SPP write start failed: ") + String(status);
      impl->txQueue[impl->txHead] = "";
      impl->txHead =
        (impl->txHead + 1) % EspBluedroidSpp::WriteQueueCapacity;
      --impl->txCount;
      ++impl->droppedWrites;
      impl->txInFlight = false;
    }
    if (impl->writeCompletionEventsEnabled.load(std::memory_order_acquire))
    {
      EspBluedroidSppImpl::Event queued;
      queued.type = EspBluedroidSppImpl::EventType::WriteCompleted;
      queued.writeResult = std::move(result);
      impl->enqueue(std::move(queued));
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
    impl->connectSecurity = EspBluedroidSppSecurity::None;
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
  EspBluedroidSppSecurity security = EspBluedroidSppSecurity::None;
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (!impl->initialized || !impl->serverStartPending || impl->ending)
    {
      return;
    }
    name = impl->serverName.c_str();
    channel = impl->serverChannel;
    security = impl->serverSecurity;
  }
  if (
    esp_bt_gap_set_scan_mode(
      ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE) != ESP_OK ||
    esp_spp_start_srv(
        sppSecurityMask(security),
        ESP_SPP_ROLE_SLAVE, channel, name) != ESP_OK)
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
    EspBluedroidSppSecurity security = EspBluedroidSppSecurity::None;
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      connecting = impl->connecting;
      memcpy(address, impl->connectBackendAddress, sizeof(address));
      security = impl->connectSecurity;
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
          sppSecurityMask(security), ESP_SPP_ROLE_MASTER,
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
        session.authenticated =
          impl->connectSecurity != EspBluedroidSppSecurity::None;
        session.encrypted =
          impl->connectSecurity ==
            EspBluedroidSppSecurity::AuthenticatedEncrypted;
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
  else if (event == ESP_SPP_SRV_OPEN_EVT)
  {
    if (parameter->srv_open.status != ESP_SPP_SUCCESS)
    {
      bool running = false;
      {
        std::lock_guard<std::mutex> lock(impl->mutex);
        running = impl->serverRunning && !impl->ending;
      }
      if (running)
      {
        esp_bt_gap_set_scan_mode(
          ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
      }
      return;
    }
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
      session.authenticated =
        impl->serverSecurity != EspBluedroidSppSecurity::None;
      session.encrypted =
        impl->serverSecurity ==
          EspBluedroidSppSecurity::AuthenticatedEncrypted;
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
    EspBluedroidSppWriteResult result;
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      if (
        parameter->write.handle != impl->backendHandle ||
        !impl->txInFlight || impl->txCount == 0)
      {
        return;
      }
      result.sessionId = impl->activeSession.id;
      result.length = impl->txQueue[impl->txHead].length();
      result.success = parameter->write.status == ESP_SPP_SUCCESS;
      if (!result.success)
      {
        result.error = EspBleError::BackendFailure;
        result.detail =
          String("SPP write failed: ") +
          String(static_cast<int>(parameter->write.status));
        ++impl->droppedWrites;
      }
      impl->txQueue[impl->txHead] = "";
      impl->txHead =
        (impl->txHead + 1) % EspBluedroidSpp::WriteQueueCapacity;
      --impl->txCount;
      impl->txInFlight = false;
      impl->txCongested = parameter->write.cong;
    }
    if (impl->writeCompletionEventsEnabled.load(std::memory_order_acquire))
    {
      EspBluedroidSppImpl::Event queued;
      queued.type = EspBluedroidSppImpl::EventType::WriteCompleted;
      queued.writeResult = std::move(result);
      impl->enqueue(std::move(queued));
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
    bool connectionAttemptClosed = false;
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      connectionAttemptClosed =
        impl->backendHandle == 0 && impl->connecting;
      if (
        !connectionAttemptClosed &&
        parameter->close.handle != impl->backendHandle)
      {
        return;
      }
      if (!connectionAttemptClosed)
      {
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
    }
    if (connectionAttemptClosed)
    {
      failSppConnection(
        impl, EspBleError::BackendFailure,
        "SPP connection closed during authentication");
      return;
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
    MtuChanged,
    ConnectionParametersUpdated,
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
    EspBleMtuChanged mtuChanged;
    EspBleSecurityChanged securityChanged;
    EspBlePasskeyDisplayed passkeyDisplayed;
    EspBleGattResult gattResult;
    EspBleGattNotification notification;
  };

  struct GattDatabaseSnapshot
  {
    EspBleConnectionId connectionId = 0;
    EspBleGattServiceInfo services[EspBleBluedroid::MaxDiscoveredGattServices];
    uint16_t serviceEndHandles[
      EspBleBluedroid::MaxDiscoveredGattServices] = {};
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
      // Arduino-ESP32's BLEClientCallbacks omits the HCI reason. The custom
      // GATTC handler below receives the same event with its full payload.
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
    delete gattDiscoveryDatabase;
    delete securityBackend;
  }

  bool pushEventLocked(const Event &event)
  {
    if (eventCount == EventCapacity)
    {
      if (event.type == EventType::Notification)
      {
        ++droppedEvents;
        return false;
      }

      size_t notificationOffset = EventCapacity;
      for (size_t offset = 0; offset < eventCount; ++offset)
      {
        if (
          events[(eventHead + offset) % EventCapacity].type ==
          EventType::Notification)
        {
          notificationOffset = offset;
          break;
        }
      }
      if (notificationOffset == EventCapacity)
      {
        ++droppedEvents;
        return false;
      }

      for (size_t offset = notificationOffset;
           offset + 1 < eventCount; ++offset)
      {
        events[(eventHead + offset) % EventCapacity] = std::move(
          events[(eventHead + offset + 1) % EventCapacity]);
      }
      --eventCount;
      ++droppedEvents;
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
    // Every new ATT bearer starts at 23. BLEClient reuses its previous m_mtu
    // value across reconnects, so getMTU() is stale until CFG_MTU completes.
    connection.mtu = 23;
    BLEAddress peerAddress = connectedClient->getPeerAddress();
    memcpy(peerBda, peerAddress.getNative(), sizeof(peerBda));
    peerBdaPresent = true;
    esp_gap_conn_params_t connectionParameters{};
    if (esp_ble_get_current_conn_params(
          peerBda, &connectionParameters) == ESP_OK)
    {
      connection.connectionInterval = connectionParameters.interval;
      connection.peripheralLatency = connectionParameters.latency;
      connection.supervisionTimeout = connectionParameters.timeout;
    }
    if (pendingConnectionParametersPresent &&
        memcmp(
          peerBda, pendingConnectionParametersBda, sizeof(peerBda)) == 0)
    {
      connection.connectionInterval = pendingConnectionInterval;
      connection.peripheralLatency = pendingConnectionLatency;
      connection.supervisionTimeout = pendingConnectionTimeout;
      pendingConnectionParametersPresent = false;
    }
    Event event;
    event.type = EventType::Connected;
    event.connection = connection;
    pushEventLocked(event);
    // Request the exchange later from update(), after BLEClient::connect() has
    // completely returned. Calling setMTU() here re-enters Bluedroid's GATT
    // command queue from its BTU callback and can overflow BTU_TASK on close.
    mtuRequestPending = preferredMtu != 23;
    mtuRequestInFlight = false;
    disconnectPending = false;
    if (pendingMtuPresent && pendingMtuHandle == connection.handle)
    {
      Event mtuEvent;
      mtuEvent.type = EventType::MtuChanged;
      mtuEvent.mtuChanged.previousMtu = connection.mtu;
      connection.mtu = pendingMtu;
      mtuEvent.mtuChanged.connection = connection;
      if (mtuEvent.mtuChanged.previousMtu != connection.mtu)
      {
        pushEventLocked(mtuEvent);
      }
      pendingMtuPresent = false;
    }
  }

  void backendDisconnected(uint16_t connectionHandle, int reason)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!active || connection.handle != connectionHandle)
    {
      return;
    }
    Event event;
    event.type = EventType::Disconnected;
    connection.disconnectReason = reason;
    event.connection = connection;
    active = false;
    connection = EspBleConnection();
    mtuRequestPending = false;
    mtuRequestInFlight = false;
    disconnectPending = false;
    peerBdaPresent = false;
    pendingMtuPresent = false;
    if (gattDatabase != nullptr)
    {
      gattDatabase->connectionId = 0;
      gattDatabase->serviceCount = 0;
      gattDatabase->characteristicCount = 0;
      gattDatabase->descriptorCount = 0;
    }
    delete gattDiscoveryDatabase;
    gattDiscoveryDatabase = nullptr;
    // An operation waiting on a Bluedroid callback will never get one once the
    // link is gone, so its completion has to be produced here. Dropping it would
    // leave an application waiting forever for a callback per request, with no
    // error to explain it.
    if (gattDirectDiscovery)
    {
      gattDirectDiscovery = false;
      gattOperating = false;
      if (!ending && !gattTimedOut)
      {
        Event failure;
        failure.type = EventType::GattResult;
        failure.gattResult.operation = EspBleGattOperation::DiscoverServices;
        failure.gattResult.connectionId = gattConnectionId;
        failure.gattResult.success = false;
        failure.gattResult.error = EspBleError::InvalidState;
        failure.gattResult.detail =
          "connection closed before the GATT operation completed";
        pushEventLocked(failure);
      }
    }
    if (gattDirectCharacteristicRead)
    {
      gattDirectCharacteristicRead = false;
      gattOperating = false;
      if (!ending && !gattTimedOut)
      {
        Event failure;
        failure.type = EventType::GattResult;
        failure.gattResult = gattDirectResult;
        failure.gattResult.success = false;
        failure.gattResult.error = EspBleError::InvalidState;
        failure.gattResult.detail =
          "connection closed before the GATT operation completed";
        pushEventLocked(failure);
      }
    }
    if (!ending)
    {
      pushEventLocked(event);
    }
  }

  void backendMtuChanged(
    uint16_t connectionHandle, esp_gatt_status_t status, uint16_t mtu)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (active && connection.handle == connectionHandle)
    {
      mtuRequestInFlight = false;
    }
    if (ending || status != ESP_GATT_OK)
    {
      return;
    }
    if (!active)
    {
      pendingMtuPresent = true;
      pendingMtuHandle = connectionHandle;
      pendingMtu = mtu;
      return;
    }
    if (connection.handle != connectionHandle || connection.mtu == mtu) return;
    Event event;
    event.type = EventType::MtuChanged;
    event.mtuChanged.previousMtu = connection.mtu;
    connection.mtu = mtu;
    event.mtuChanged.connection = connection;
    pushEventLocked(event);
  }

  void backendDiscoveryService(
    esp_gatt_if_t gattcIf,
    uint16_t connectionHandle,
    uint16_t startHandle,
    uint16_t endHandle,
    const esp_bt_uuid_t &serviceUuid)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!gattDirectDiscovery ||
        gattcIf != gattDirectGattcIf ||
        connectionHandle != gattDirectConnectionHandle ||
        gattDiscoveryDatabase == nullptr ||
        gattDiscoveryError != EspBleError::None)
    {
      return;
    }
    if (gattDiscoveryDatabase->serviceCount ==
        EspBleBluedroid::MaxDiscoveredGattServices)
    {
      gattDiscoveryError = EspBleError::ResourceExhausted;
      gattDiscoveryDetail = "too many discovered GATT services";
      return;
    }
    const size_t index = gattDiscoveryDatabase->serviceCount++;
    EspBleGattServiceInfo &service =
      gattDiscoveryDatabase->services[index];
    service.serviceUuid = formatBackendUuid(serviceUuid);
    service.handle = startHandle;
    gattDiscoveryDatabase->serviceEndHandles[index] = endHandle;
  }

  void backendDiscoveryCompleted(
    esp_gatt_if_t gattcIf,
    uint16_t connectionHandle,
    esp_gatt_status_t status)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!gattDirectDiscovery ||
        gattcIf != gattDirectGattcIf ||
        connectionHandle != gattDirectConnectionHandle)
    {
      return;
    }

    EspBleGattResult result;
    result.operation = EspBleGattOperation::DiscoverServices;
    result.connectionId = gattConnectionId;
    if (status != ESP_GATT_OK)
    {
      gattDiscoveryError = EspBleError::BackendFailure;
      gattDiscoveryDetail =
        String("GATT service discovery failed: ") +
        String(static_cast<unsigned>(status));
    }

    GattDatabaseSnapshot *database = gattDiscoveryDatabase;
    if (gattDiscoveryError == EspBleError::None && database != nullptr)
    {
      for (size_t serviceIndex = 0;
           serviceIndex < database->serviceCount;
           ++serviceIndex)
      {
        uint16_t characteristicOffset = 0;
        while (true)
        {
          esp_gattc_char_elem_t characteristic{};
          uint16_t count = 1;
          const esp_gatt_status_t characteristicStatus =
            esp_ble_gattc_get_all_char(
              gattcIf,
              connectionHandle,
              database->services[serviceIndex].handle,
              database->serviceEndHandles[serviceIndex],
              &characteristic,
              &count,
              characteristicOffset);
          if (characteristicStatus == ESP_GATT_INVALID_OFFSET ||
              characteristicStatus == ESP_GATT_NOT_FOUND ||
              count == 0)
          {
            break;
          }
          if (characteristicStatus != ESP_GATT_OK)
          {
            gattDiscoveryError = EspBleError::BackendFailure;
            gattDiscoveryDetail =
              String("failed to enumerate GATT characteristics: ") +
              String(static_cast<unsigned>(characteristicStatus));
            break;
          }
          if (database->characteristicCount ==
              EspBleBluedroid::MaxDiscoveredGattCharacteristics)
          {
            gattDiscoveryError = EspBleError::ResourceExhausted;
            gattDiscoveryDetail =
              "too many discovered GATT characteristics";
            break;
          }

          EspBleGattCharacteristicInfo &characteristicInfo =
            database->characteristics[database->characteristicCount++];
          characteristicInfo.serviceUuid =
            database->services[serviceIndex].serviceUuid;
          characteristicInfo.characteristicUuid =
            formatBackendUuid(characteristic.uuid);
          characteristicInfo.handle = characteristic.char_handle;
          characteristicInfo.readable =
            (characteristic.properties & ESP_GATT_CHAR_PROP_BIT_READ) != 0;
          characteristicInfo.writable =
            (characteristic.properties & ESP_GATT_CHAR_PROP_BIT_WRITE) != 0;
          characteristicInfo.writableWithoutResponse =
            (characteristic.properties &
             ESP_GATT_CHAR_PROP_BIT_WRITE_NR) != 0;
          characteristicInfo.notifiable =
            (characteristic.properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY) != 0;
          characteristicInfo.indicatable =
            (characteristic.properties &
             ESP_GATT_CHAR_PROP_BIT_INDICATE) != 0;

          uint16_t descriptorOffset = 0;
          while (true)
          {
            esp_gattc_descr_elem_t descriptor{};
            uint16_t descriptorCount = 1;
            const esp_gatt_status_t descriptorStatus =
              esp_ble_gattc_get_all_descr(
                gattcIf,
                connectionHandle,
                characteristic.char_handle,
                &descriptor,
                &descriptorCount,
                descriptorOffset);
            if (descriptorStatus == ESP_GATT_INVALID_OFFSET ||
                descriptorStatus == ESP_GATT_NOT_FOUND ||
                descriptorCount == 0)
            {
              break;
            }
            if (descriptorStatus != ESP_GATT_OK)
            {
              gattDiscoveryError = EspBleError::BackendFailure;
              gattDiscoveryDetail =
                String("failed to enumerate GATT descriptors: ") +
                String(static_cast<unsigned>(descriptorStatus));
              break;
            }
            if (database->descriptorCount ==
                EspBleBluedroid::MaxDiscoveredGattDescriptors)
            {
              gattDiscoveryError = EspBleError::ResourceExhausted;
              gattDiscoveryDetail =
                "too many discovered GATT descriptors";
              break;
            }
            EspBleGattDescriptorInfo &descriptorInfo =
              database->descriptors[database->descriptorCount++];
            descriptorInfo.serviceUuid = characteristicInfo.serviceUuid;
            descriptorInfo.characteristicUuid =
              characteristicInfo.characteristicUuid;
            descriptorInfo.descriptorUuid =
              formatBackendUuid(descriptor.uuid);
            descriptorInfo.handle = descriptor.handle;
            descriptorInfo.characteristicHandle = characteristicInfo.handle;
            ++descriptorOffset;
          }
          if (gattDiscoveryError != EspBleError::None) break;
          ++characteristicOffset;
        }
        if (gattDiscoveryError != EspBleError::None) break;
      }
    }

    result.success =
      gattDiscoveryError == EspBleError::None && database != nullptr;
    result.error = gattDiscoveryError;
    result.detail = gattDiscoveryDetail;
    if (!ending && !gattTimedOut)
    {
      if (result.success)
      {
        delete gattDatabase;
        gattDatabase = database;
        gattDiscoveryDatabase = nullptr;
      }
      Event event;
      event.type = EventType::GattResult;
      event.gattResult = result;
      pushEventLocked(event);
    }
    delete gattDiscoveryDatabase;
    gattDiscoveryDatabase = nullptr;
    gattDirectDiscovery = false;
    gattOperating = false;
  }

  void backendCharacteristicReadCompleted(
    esp_gatt_if_t gattcIf,
    uint16_t connectionHandle,
    esp_gatt_status_t status,
    uint16_t handle,
    const uint8_t *value,
    size_t length)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!gattDirectCharacteristicRead ||
        gattcIf != gattDirectGattcIf ||
        connectionHandle != gattDirectConnectionHandle ||
        handle != gattDirectResult.handle)
    {
      return;
    }
    EspBleGattResult result = gattDirectResult;
    result.success = status == ESP_GATT_OK;
    if (result.success)
    {
      result.value = length == 0
        ? String()
        : String(reinterpret_cast<const char *>(value), length);
    }
    else
    {
      result.error = EspBleError::BackendFailure;
      result.detail =
        String("GATT characteristic read failed: ") +
        String(static_cast<unsigned>(status));
    }
    if (!ending && !gattTimedOut)
    {
      Event event;
      event.type = EventType::GattResult;
      event.gattResult = result;
      pushEventLocked(event);
    }
    gattDirectCharacteristicRead = false;
    gattOperating = false;
  }

  static void customGattcHandler(
    esp_gattc_cb_event_t event,
    esp_gatt_if_t gattcIf,
    esp_ble_gattc_cb_param_t *param)
  {
    EspBleConnectionImpl *owner = customGattcOwner;
    if (owner == nullptr || param == nullptr) return;
    if (event == ESP_GATTC_CFG_MTU_EVT)
    {
      owner->backendMtuChanged(
        param->cfg_mtu.conn_id, param->cfg_mtu.status, param->cfg_mtu.mtu);
    }
    else if (event == ESP_GATTC_DISCONNECT_EVT)
    {
      owner->backendDisconnected(
        param->disconnect.conn_id, param->disconnect.reason);
    }
    else if (event == ESP_GATTC_SEARCH_RES_EVT)
    {
      owner->backendDiscoveryService(
        gattcIf,
        param->search_res.conn_id,
        param->search_res.start_handle,
        param->search_res.end_handle,
        param->search_res.srvc_id.uuid);
    }
    else if (event == ESP_GATTC_SEARCH_CMPL_EVT)
    {
      owner->backendDiscoveryCompleted(
        gattcIf,
        param->search_cmpl.conn_id,
        param->search_cmpl.status);
    }
    else if (event == ESP_GATTC_READ_CHAR_EVT)
    {
      owner->backendCharacteristicReadCompleted(
        gattcIf,
        param->read.conn_id,
        param->read.status,
        param->read.handle,
        param->read.value,
        param->read.value_len);
    }
  }

  static EspBleConnectionImpl *customGattcOwner;

  void requestPreferredMtu()
  {
    BLEClient *connectedClient = nullptr;
    uint16_t requestedMtu = 23;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (!mtuRequestPending || connecting || !active || client == nullptr)
      {
        return;
      }
      mtuRequestPending = false;
      mtuRequestInFlight = true;
      connectedClient = client;
      requestedMtu = preferredMtu;
    }
    if (!connectedClient->setMTU(requestedMtu))
    {
      std::lock_guard<std::mutex> lock(mutex);
      mtuRequestInFlight = false;
    }
  }

  void requestPendingDisconnect()
  {
    BLEClient *connectedClient = nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (!disconnectPending || mtuRequestInFlight || !active ||
          client == nullptr)
      {
        return;
      }
      disconnectPending = false;
      connectedClient = client;
    }
    connectedClient->disconnect();
  }

  void backendConnectionParametersUpdated(
    esp_bt_status_t status,
    const esp_bd_addr_t address,
    uint16_t interval,
    uint16_t latency,
    uint16_t timeout)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (ending || status != ESP_BT_STATUS_SUCCESS) return;
    if (!active || !peerBdaPresent)
    {
      pendingConnectionParametersPresent = true;
      memcpy(
        pendingConnectionParametersBda, address,
        sizeof(pendingConnectionParametersBda));
      pendingConnectionInterval = interval;
      pendingConnectionLatency = latency;
      pendingConnectionTimeout = timeout;
      return;
    }
    if (memcmp(peerBda, address, sizeof(peerBda)) != 0) return;
    connection.connectionInterval = interval;
    connection.peripheralLatency = latency;
    connection.supervisionTimeout = timeout;
    Event event;
    event.type = EventType::ConnectionParametersUpdated;
    event.connection = connection;
    pushEventLocked(event);
  }

  static void customGapHandler(
    esp_gap_ble_cb_event_t event,
    esp_ble_gap_cb_param_t *param)
  {
    EspBleScannerImpl *scanner =
      activeBleScanner.load(std::memory_order_acquire);
    if (scanner != nullptr && param != nullptr)
    {
      scanner->handleGapEvent(event, param);
    }
    EspBleConnectionImpl *owner = customGapOwner;
    if (owner == nullptr || param == nullptr)
    {
      return;
    }
    if (event == ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT)
    {
      owner->backendConnectionParametersUpdated(
        param->update_conn_params.status,
        param->update_conn_params.bda,
        param->update_conn_params.conn_int,
        param->update_conn_params.latency,
        param->update_conn_params.timeout);
    }
    else if (event == ESP_GAP_BLE_SET_STATIC_RAND_ADDR_EVT)
    {
      owner->randomAddressOperationSucceeded.store(
        param->set_rand_addr_cmpl.status == ESP_BT_STATUS_SUCCESS,
        std::memory_order_release);
      owner->randomAddressOperationCompleted.store(
        true, std::memory_order_release);
    }
    else if (event == ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT)
    {
      owner->privacyOperationSucceeded.store(
        param->local_privacy_cmpl.status == ESP_BT_STATUS_SUCCESS,
        std::memory_order_release);
      owner->privacyOperationCompleted.store(
        true, std::memory_order_release);
    }
    else if (event == ESP_GAP_BLE_UPDATE_WHITELIST_COMPLETE_EVT)
    {
      owner->acceptListOperationSucceeded.store(
        param->update_whitelist_cmpl.status == ESP_BT_STATUS_SUCCESS,
        std::memory_order_release);
      owner->acceptListOperationCompleted.store(
        true, std::memory_order_release);
    }
    else if (event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT)
    {
      owner->advertisingStartOperationSucceeded.store(
        param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS,
        std::memory_order_release);
      owner->advertisingStartOperationCompleted.store(
        true, std::memory_order_release);
    }
  }

  static EspBleConnectionImpl *customGapOwner;

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
    {
      std::lock_guard<std::mutex> lock(impl->mutex);
      result.operation = impl->gattOperation;
      result.connectionId = impl->gattConnectionId;
      result.serviceUuid = impl->gattServiceUuid;
      result.characteristicUuid = impl->gattCharacteristicUuid;
      result.descriptorUuid = impl->gattDescriptorUuid;
      result.handle = impl->gattCharacteristicHandle;
      result.descriptorHandle = impl->gattDescriptorHandle;
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
      result.error = EspBleError::InvalidState;
      result.detail = "GATT discovery was not started through the direct backend";
    }
    else
    {
      BLERemoteCharacteristic *characteristic = nullptr;
      BLERemoteDescriptor *selectedDescriptor = nullptr;
      if (result.descriptorHandle != 0)
      {
        selectedDescriptor = findDescriptorByHandle(
          client, result.descriptorHandle, result.serviceUuid, characteristic);
        if (selectedDescriptor == nullptr || characteristic == nullptr)
        {
          result.error = EspBleError::NotFound;
          result.detail =
            "GATT descriptor handle was not found (discover services first)";
        }
        else
        {
          result.characteristicUuid = characteristic->getUUID().toString();
          result.descriptorUuid = selectedDescriptor->getUUID().toString();
        }
      }
      else if (result.handle != 0)
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
            BLERemoteDescriptor *descriptor = selectedDescriptor != nullptr
              ? selectedDescriptor
              : characteristic->getDescriptor(BLEUUID(result.descriptorUuid.c_str()));
            if (descriptor == nullptr)
            {
              result.error = EspBleError::NotFound;
              result.detail = "GATT descriptor was not found";
            }
            else
            {
              result.descriptorHandle = descriptor->getHandle();
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
          else if (result.operation == EspBleGattOperation::Discover)
          {
            result.success = true;
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
  uint16_t gattDescriptorHandle = 0;
  String gattWriteValue;
  bool gattWriteResponse = true;
  uint32_t gattStartedAt = 0;
  uint32_t gattTimeoutMilliseconds = 10000;
  bool gattTimedOut = false;
  GattDatabaseSnapshot *gattDatabase = nullptr;
  GattDatabaseSnapshot *gattDiscoveryDatabase = nullptr;
  bool gattDirectDiscovery = false;
  esp_gatt_if_t gattDirectGattcIf = ESP_GATT_IF_NONE;
  uint16_t gattDirectConnectionHandle = 0xffff;
  EspBleError gattDiscoveryError = EspBleError::None;
  String gattDiscoveryDetail;
  bool gattDirectCharacteristicRead = false;
  EspBleGattResult gattDirectResult;
  EspBleScanResult target;
  uint32_t timeoutMilliseconds = 10000;
  uint16_t preferredMtu = 247;
  EspBleConnection connection;
  bool mtuRequestPending = false;
  bool mtuRequestInFlight = false;
  bool disconnectPending = false;
  bool pendingMtuPresent = false;
  uint16_t pendingMtuHandle = 0xffff;
  uint16_t pendingMtu = 23;
  bool peerBdaPresent = false;
  esp_bd_addr_t peerBda{};
  bool pendingConnectionParametersPresent = false;
  esp_bd_addr_t pendingConnectionParametersBda{};
  uint16_t pendingConnectionInterval = 0;
  uint16_t pendingConnectionLatency = 0;
  uint16_t pendingConnectionTimeout = 0;
  std::atomic<bool> randomAddressOperationCompleted{false};
  std::atomic<bool> randomAddressOperationSucceeded{false};
  std::atomic<bool> privacyOperationCompleted{false};
  std::atomic<bool> privacyOperationSucceeded{false};
  std::atomic<bool> acceptListOperationCompleted{false};
  std::atomic<bool> acceptListOperationSucceeded{false};
  std::atomic<bool> advertisingStartOperationCompleted{false};
  std::atomic<bool> advertisingStartOperationSucceeded{false};
  EspBleConnectionId nextConnectionId = 1;
  Event events[EventCapacity];
  size_t eventHead = 0;
  size_t eventCount = 0;
  size_t droppedEvents = 0;
};

EspBleConnectionImpl *EspBleConnectionImpl::customGattcOwner = nullptr;
EspBleConnectionImpl *EspBleConnectionImpl::customGapOwner = nullptr;

namespace
{
bool waitForAcceptListOperation(EspBleConnectionImpl *impl)
{
  const uint32_t startedAt = millis();
  while (!impl->acceptListOperationCompleted.load(std::memory_order_acquire) &&
         static_cast<uint32_t>(millis() - startedAt) < 2000)
  {
    delay(1);
  }
  return
    impl->acceptListOperationCompleted.load(std::memory_order_acquire) &&
    impl->acceptListOperationSucceeded.load(std::memory_order_acquire);
}

void prepareAcceptListOperation(EspBleConnectionImpl *impl)
{
  impl->acceptListOperationCompleted.store(false, std::memory_order_release);
  impl->acceptListOperationSucceeded.store(false, std::memory_order_release);
}

esp_ble_wl_addr_type_t acceptListBackendAddressType(
  EspBleAddressType type)
{
  return type == EspBleAddressType::Random ||
      type == EspBleAddressType::RandomIdentity
    ? BLE_WL_ADDR_TYPE_RANDOM
    : BLE_WL_ADDR_TYPE_PUBLIC;
}
} // namespace

bool EspBleScanResult::hasName() const
{
  return !name.isEmpty();
}

bool EspBleScanResult::hasManufacturerData() const
{
  return !manufacturerData.isEmpty();
}

bool EspBleScanResult::hasServiceData() const
{
  return serviceDataCount != 0;
}

bool EspBleScanResult::hasAppearance() const
{
  return appearance != 0;
}

bool EspBleScanResult::hasTxPowerLevel() const
{
  return txPowerLevelPresent;
}

bool EspBleScanResult::serviceDataFor(
  const char *uuid, String &data) const
{
  if (uuid == nullptr || uuid[0] == '\0')
  {
    return false;
  }
  for (size_t index = 0; index < serviceDataCount; ++index)
  {
    if (uuidEquals(serviceData[index].uuid, uuid))
    {
      data = serviceData[index].data;
      return true;
    }
  }
  return false;
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

void EspBleAdvertisingData::clear()
{
  name_ = "";
  manufacturerData_ = "";
  for (EspBleServiceData &block : serviceData_)
  {
    block = EspBleServiceData();
  }
  serviceDataCount_ = 0;
  serviceUuidCount_ = 0;
  appearance_ = 0;
  txPowerIncluded_ = false;
}

void EspBleAdvertisingData::setName(const char *name)
{
  name_ = name == nullptr ? "" : name;
}

bool EspBleAdvertisingData::addServiceUuid(const char *uuid)
{
  if (uuid == nullptr || uuid[0] == '\0')
  {
    return false;
  }
  for (size_t index = 0; index < serviceUuidCount_; ++index)
  {
    if (uuidEquals(serviceUuids_[index], uuid))
    {
      return true;
    }
  }
  if (serviceUuidCount_ == MaxServiceUuids)
  {
    return false;
  }
  serviceUuids_[serviceUuidCount_++] = uuid;
  return true;
}

void EspBleAdvertisingData::setManufacturerData(
  const uint8_t *data, size_t length)
{
  manufacturerData_ = data == nullptr || length == 0
    ? String()
    : String(reinterpret_cast<const char *>(data), length);
}

bool EspBleAdvertisingData::addServiceData(
  const char *uuid, const uint8_t *data, size_t length)
{
  if (uuid == nullptr || uuid[0] == '\0')
  {
    return false;
  }

  size_t slot = serviceDataCount_;
  for (size_t index = 0; index < serviceDataCount_; ++index)
  {
    if (uuidEquals(serviceData_[index].uuid, uuid))
    {
      slot = index;
      break;
    }
  }

  if (data == nullptr || length == 0)
  {
    if (slot == serviceDataCount_)
    {
      return true;
    }
    for (size_t next = slot + 1; next < serviceDataCount_; ++next)
    {
      serviceData_[next - 1] = serviceData_[next];
    }
    serviceData_[--serviceDataCount_] = EspBleServiceData();
    return true;
  }

  if (slot == serviceDataCount_)
  {
    if (serviceDataCount_ == MaxServiceData)
    {
      return false;
    }
    ++serviceDataCount_;
  }
  serviceData_[slot].uuid = uuid;
  serviceData_[slot].data =
    String(reinterpret_cast<const char *>(data), length);
  return true;
}

void EspBleAdvertisingData::setAppearance(uint16_t appearance)
{
  appearance_ = appearance;
}

void EspBleAdvertisingData::setTxPowerIncluded(bool included)
{
  txPowerIncluded_ = included;
}

bool EspBleAdvertisingData::isEmpty() const
{
  return name_.isEmpty() && manufacturerData_.isEmpty() &&
    serviceDataCount_ == 0 && serviceUuidCount_ == 0 &&
    appearance_ == 0 && !txPowerIncluded_;
}

EspBleAdvertising::EspBleAdvertising(EspBleBluedroid *owner) : owner_(owner) {}

void EspBleAdvertising::clear()
{
  data_.clear();
  scanResponseData_.clear();
  scanResponseEnabled_ = true;
  filterPolicy_ = EspBleAdvertisingFilterPolicy::Any;
  connectable_ = true;
  intervalMinMs_ = 0;
  intervalMaxMs_ = 0;
  directed_ = false;
  directedHighDuty_ = false;
  directedAddress_ = String();
  directedAddressType_ = EspBleAddressType::Public;
  channelMask_ = 0;
}

EspBleAdvertisingData &EspBleAdvertising::data()
{
  return data_;
}

EspBleAdvertisingData &EspBleAdvertising::scanResponse()
{
  return scanResponseData_;
}

void EspBleAdvertising::setName(const char *name)
{
  data_.setName(name);
}

bool EspBleAdvertising::addServiceUuid(const char *uuid)
{
  if (uuid == nullptr || uuid[0] == '\0')
  {
    owner_->setError(EspBleError::InvalidArgument, "service UUID is empty");
    return false;
  }
  if (!data_.addServiceUuid(uuid))
  {
    owner_->setError(
      EspBleError::ResourceExhausted, "too many advertising service UUIDs");
    return false;
  }
  owner_->clearError();
  return true;
}

void EspBleAdvertising::setManufacturerData(const uint8_t *data, size_t length)
{
  data_.setManufacturerData(data, length);
}

bool EspBleAdvertising::addServiceData(
  const char *uuid, const uint8_t *data, size_t length)
{
  if (uuid == nullptr || uuid[0] == '\0')
  {
    owner_->setError(
      EspBleError::InvalidArgument, "service data UUID is required");
    return false;
  }
  if (!data_.addServiceData(uuid, data, length))
  {
    owner_->setError(
      EspBleError::ResourceExhausted,
      "too many advertising service data blocks");
    return false;
  }
  owner_->clearError();
  return true;
}

void EspBleAdvertising::setAppearance(uint16_t appearance)
{
  data_.setAppearance(appearance);
}

void EspBleAdvertising::setScanResponseEnabled(bool enabled)
{
  scanResponseEnabled_ = enabled;
}

void EspBleAdvertising::setFilterPolicy(
  EspBleAdvertisingFilterPolicy policy)
{
  filterPolicy_ = policy;
}

EspBleAdvertisingFilterPolicy EspBleAdvertising::filterPolicy() const
{
  return filterPolicy_;
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

bool EspBleAdvertising::setDirectedTarget(
  const char *address,
  EspBleAddressType addressType,
  bool highDuty)
{
  if (!isValidBleAddress(address) ||
      static_cast<uint8_t>(addressType) >
        static_cast<uint8_t>(EspBleAddressType::RandomIdentity))
  {
    owner_->setError(
      EspBleError::InvalidArgument,
      "valid directed peer address and address type are required");
    return false;
  }
  directed_ = true;
  directedHighDuty_ = highDuty;
  directedAddress_ = address;
  directedAddressType_ = addressType;
  owner_->clearError();
  return true;
}

void EspBleAdvertising::clearDirectedTarget()
{
  directed_ = false;
  directedHighDuty_ = false;
  directedAddress_ = String();
}

bool EspBleAdvertising::setChannelMap(uint8_t channelMask)
{
  if ((channelMask &
       ~static_cast<uint8_t>(EspBleAdvertisingChannelAll)) != 0)
  {
    owner_->setError(
      EspBleError::InvalidArgument,
      "advertising channel mask is invalid");
    return false;
  }
  channelMask_ = channelMask;
  owner_->clearError();
  return true;
}

bool EspBleAdvertising::applyOwnAddress()
{
  if (!owner_->activeRandomAddressPresent_) return true;

  BLEAdvertising *backend = BLEDevice::getAdvertising();
  EspBleConnectionImpl *operations = owner_->connectionImpl_;
  const auto waitFor = [](std::atomic<bool> &completed) {
    const uint32_t startedAt = millis();
    while (!completed.load(std::memory_order_acquire) &&
           static_cast<uint32_t>(millis() - startedAt) < 2000)
    {
      delay(1);
    }
    return completed.load(std::memory_order_acquire);
  };
  if (owner_->activeOwnAddressType_ ==
      EspBleOwnAddressType::ResolvablePrivate)
  {
    operations->privacyOperationCompleted.store(
      false, std::memory_order_release);
    operations->privacyOperationSucceeded.store(
      false, std::memory_order_release);
    if (esp_ble_gap_config_local_privacy(false) != ESP_OK ||
        !waitFor(operations->privacyOperationCompleted) ||
        !operations->privacyOperationSucceeded.load(
          std::memory_order_acquire))
    {
      owner_->setError(
        EspBleError::BackendFailure,
        "failed to disable BLE privacy before setting the identity");
      return false;
    }
  }
  esp_bd_addr_t address;
  memcpy(
    address, owner_->activeRandomAddress_,
    sizeof(owner_->activeRandomAddress_));
  const esp_ble_addr_type_t addressType =
    owner_->activeOwnAddressType_ ==
        EspBleOwnAddressType::ResolvablePrivate
      ? BLE_ADDR_TYPE_RPA_RANDOM
      : BLE_ADDR_TYPE_RANDOM;
  operations->randomAddressOperationCompleted.store(
    false, std::memory_order_release);
  operations->randomAddressOperationSucceeded.store(
    false, std::memory_order_release);
  if (!backend->setDeviceAddress(address, addressType) ||
      !waitFor(operations->randomAddressOperationCompleted) ||
      !operations->randomAddressOperationSucceeded.load(
        std::memory_order_acquire))
  {
    owner_->setError(
      EspBleError::BackendFailure,
      "failed to apply the advertising address");
    return false;
  }
  if (owner_->activeOwnAddressType_ ==
      EspBleOwnAddressType::ResolvablePrivate)
  {
    operations->privacyOperationCompleted.store(
      false, std::memory_order_release);
    operations->privacyOperationSucceeded.store(
      false, std::memory_order_release);
    if (esp_ble_gap_config_local_privacy(true) != ESP_OK ||
        !waitFor(operations->privacyOperationCompleted) ||
        !operations->privacyOperationSucceeded.load(
          std::memory_order_acquire))
    {
      owner_->setError(
        EspBleError::BackendFailure, "failed to enable BLE privacy");
      return false;
    }
  }
  return true;
}

bool EspBleAdvertising::start(uint32_t durationSeconds)
{
  if (!owner_->initialized())
  {
    owner_->setError(EspBleError::InvalidState, "BLE stack is not initialized");
    return false;
  }
  if (static_cast<uint8_t>(filterPolicy_) >
      static_cast<uint8_t>(EspBleAdvertisingFilterPolicy::Both))
  {
    owner_->setError(
      EspBleError::InvalidArgument,
      "unsupported advertising filter policy");
    return false;
  }

  if (advertising_ && !stop()) return false;

  if (directed_)
  {
    if (!applyOwnAddress()) return false;

    esp_ble_adv_params_t parameters{};
    if (directedHighDuty_)
    {
      parameters.adv_int_min = 0x0020;
      parameters.adv_int_max = 0x0020;
      parameters.adv_type = ADV_TYPE_DIRECT_IND_HIGH;
    }
    else
    {
      parameters.adv_int_min = intervalMinMs_ == 0
        ? 0x0800
        : static_cast<uint16_t>(
            (static_cast<uint32_t>(intervalMinMs_) * 8) / 5);
      parameters.adv_int_max = intervalMaxMs_ == 0
        ? 0x0800
        : static_cast<uint16_t>(
            (static_cast<uint32_t>(intervalMaxMs_) * 8) / 5);
      parameters.adv_type = ADV_TYPE_DIRECT_IND_LOW;
    }
    parameters.own_addr_type =
      owner_->activeOwnAddressType_ == EspBleOwnAddressType::Public
        ? BLE_ADDR_TYPE_PUBLIC
        : (owner_->activeOwnAddressType_ ==
               EspBleOwnAddressType::ResolvablePrivate
             ? BLE_ADDR_TYPE_RPA_RANDOM
             : BLE_ADDR_TYPE_RANDOM);
    if (!espblebluedroid::internal::parseBleAddress(
          directedAddress_.c_str(), parameters.peer_addr))
    {
      owner_->setError(
        EspBleError::InvalidArgument, "invalid directed peer address");
      return false;
    }
    parameters.peer_addr_type =
      directedAddressType_ == EspBleAddressType::Random ||
          directedAddressType_ == EspBleAddressType::RandomIdentity
        ? BLE_ADDR_TYPE_RANDOM
        : BLE_ADDR_TYPE_PUBLIC;
    parameters.channel_map = static_cast<esp_ble_adv_channel_t>(
      channelMask_ == 0 ? EspBleAdvertisingChannelAll : channelMask_);
    parameters.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

    EspBleConnectionImpl *operations = owner_->connectionImpl_;
    operations->advertisingStartOperationCompleted.store(
      false, std::memory_order_release);
    operations->advertisingStartOperationSucceeded.store(
      false, std::memory_order_release);
    if (esp_ble_gap_start_advertising(&parameters) != ESP_OK)
    {
      owner_->setError(
        EspBleError::BackendFailure,
        "failed to request directed advertising");
      return false;
    }
    const uint32_t requestedAt = millis();
    while (!operations->advertisingStartOperationCompleted.load(
             std::memory_order_acquire) &&
           static_cast<uint32_t>(millis() - requestedAt) < 2000)
    {
      delay(1);
    }
    if (!operations->advertisingStartOperationCompleted.load(
          std::memory_order_acquire) ||
        !operations->advertisingStartOperationSucceeded.load(
          std::memory_order_acquire))
    {
      owner_->setError(
        EspBleError::BackendFailure,
        "directed advertising did not start");
      return false;
    }

    advertising_ = true;
    directedAdvertising_ = true;
    directedHighDutyCycle_ = directedHighDuty_;
    startedAtMs_ = millis();
    durationMs_ = durationSeconds == 0 ? 0 : durationSeconds * 1000UL;
    owner_->clearError();
    return true;
  }

  BLEAdvertising *backend = BLEDevice::getAdvertising();
  backend->reset();
  if (filterPolicy_ != EspBleAdvertisingFilterPolicy::Any)
  {
    prepareAcceptListOperation(owner_->connectionImpl_);
    if (esp_ble_gap_clear_whitelist() != ESP_OK ||
        !waitForAcceptListOperation(owner_->connectionImpl_))
    {
      owner_->setError(
        EspBleError::BackendFailure, "failed to clear the BLE accept list");
      return false;
    }
    for (size_t index = 0; index < owner_->acceptListCount_; ++index)
    {
      esp_bd_addr_t address = {};
      if (!espblebluedroid::internal::parseBleAddress(
            owner_->acceptList_[index].peerAddress.c_str(), address))
      {
        owner_->setError(
          EspBleError::InvalidArgument,
          "invalid BLE accept list address");
        return false;
      }
      prepareAcceptListOperation(owner_->connectionImpl_);
      if (esp_ble_gap_update_whitelist(
            true,
            address,
            acceptListBackendAddressType(
              owner_->acceptList_[index].peerAddressType)) != ESP_OK ||
          !waitForAcceptListOperation(owner_->connectionImpl_))
      {
        owner_->setError(
          EspBleError::BackendFailure,
          "failed to write the BLE accept list");
        return false;
      }
    }
  }
  if (!applyOwnAddress()) return false;

  const auto buildPayload = [this](
    const EspBleAdvertisingData &source,
    BLEAdvertisementData &destination,
    bool includeFlags,
    const char *payloadName) {
    espblebluedroid::internal::LegacyAdvertisingData raw;
    const auto append = [this, &raw, payloadName](
      uint8_t type, const String &value, const char *field) {
      if (raw.append(
            type,
            reinterpret_cast<const uint8_t *>(value.c_str()),
            value.length()))
      {
        return true;
      }
      String detail(field);
      detail += " does not fit in legacy ";
      detail += payloadName;
      detail += " payload";
      owner_->setError(EspBleError::InvalidArgument, detail.c_str());
      return false;
    };

    if (includeFlags &&
        !append(ESP_BLE_AD_TYPE_FLAG, String("\x06", 1), "flags"))
    {
      return false;
    }
    if (source.txPowerIncluded_)
    {
      const int8_t txPower = static_cast<int8_t>(
        BLEDevice::getPower(ESP_BLE_PWR_TYPE_ADV));
      if (!append(
            ESP_BLE_AD_TYPE_TX_PWR,
            String(reinterpret_cast<const char *>(&txPower), sizeof(txPower)),
            "Tx Power Level"))
      {
        return false;
      }
    }
    if (source.appearance_ != 0)
    {
      const String appearance(
        reinterpret_cast<const char *>(&source.appearance_),
        sizeof(source.appearance_));
      if (!append(
            ESP_BLE_AD_TYPE_APPEARANCE, appearance, "appearance"))
      {
        return false;
      }
    }
    if (source.serviceUuidCount_ > 0)
    {
      String uuids16;
      String uuids32;
      String uuids128;
      for (size_t index = 0; index < source.serviceUuidCount_; ++index)
      {
        espblebluedroid::internal::BleUuid uuid;
        if (!espblebluedroid::internal::parseBleUuid(
              source.serviceUuids_[index].c_str(), uuid))
        {
          owner_->setError(
            EspBleError::InvalidArgument,
            "invalid advertising service UUID");
          return false;
        }
        switch (uuid.bitSize)
        {
        case 16:
          uuids16 += String(reinterpret_cast<const char *>(
            uuid.bytes.data()), 2);
          break;
        case 32:
          uuids32 += String(reinterpret_cast<const char *>(
            uuid.bytes.data()), 4);
          break;
        case 128:
          uuids128 += String(reinterpret_cast<const char *>(
            uuid.bytes.data()), 16);
          break;
        default:
          owner_->setError(
            EspBleError::InvalidArgument,
            "invalid advertising service UUID");
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
            !append(list.type, *list.values, "service UUIDs"))
        {
          return false;
        }
      }
    }
    if (!source.manufacturerData_.isEmpty() &&
        !append(
          ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE,
          source.manufacturerData_,
          "manufacturer data"))
    {
      return false;
    }
    for (size_t index = 0; index < source.serviceDataCount_; ++index)
    {
      const EspBleServiceData &block = source.serviceData_[index];
      espblebluedroid::internal::BleUuid uuid;
      if (!espblebluedroid::internal::parseBleUuid(
            block.uuid.c_str(), uuid))
      {
        owner_->setError(
          EspBleError::InvalidArgument, "invalid service data UUID");
        return false;
      }
      String encodedUuid;
      uint8_t type = 0;
      switch (uuid.bitSize)
      {
      case 16:
        type = ESP_BLE_AD_TYPE_SERVICE_DATA;
        encodedUuid = String(reinterpret_cast<const char *>(
          uuid.bytes.data()), 2);
        break;
      case 32:
        type = ESP_BLE_AD_TYPE_32SERVICE_DATA;
        encodedUuid = String(reinterpret_cast<const char *>(
          uuid.bytes.data()), 4);
        break;
      case 128:
        type = ESP_BLE_AD_TYPE_128SERVICE_DATA;
        encodedUuid = String(reinterpret_cast<const char *>(
          uuid.bytes.data()), 16);
        break;
      default:
        owner_->setError(
          EspBleError::InvalidArgument, "invalid service data UUID");
        return false;
      }
      if (!append(type, encodedUuid + block.data, "service data"))
      {
        return false;
      }
    }
    if (!source.name_.isEmpty() &&
        !append(ESP_BLE_AD_TYPE_NAME_CMPL, source.name_, "name"))
    {
      return false;
    }
    destination.addData(String(
      reinterpret_cast<const char *>(raw.data()), raw.size()));
    if (destination.getPayload().length() != raw.size())
    {
      owner_->setError(
        EspBleError::BackendFailure,
        "failed to build legacy advertising payload");
      return false;
    }
    return true;
  };

  const bool autoNameInScanResponse =
    scanResponseEnabled_ && scanResponseData_.isEmpty() &&
    !data_.name_.isEmpty();
  EspBleAdvertisingData primary = data_;
  if (autoNameInScanResponse)
  {
    primary.name_ = "";
  }

  BLEAdvertisementData advertisingData;
  if (!buildPayload(primary, advertisingData, true, "advertising"))
  {
    return false;
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
    EspBleAdvertisingData responseSource = scanResponseData_;
    if (autoNameInScanResponse)
    {
      responseSource.setName(data_.name_.c_str());
    }
    if (!responseSource.isEmpty())
    {
      BLEAdvertisementData responseData;
      if (!buildPayload(
            responseSource, responseData, false, "scan response"))
      {
        return false;
      }
      if (!backend->setScanResponseData(responseData))
      {
        owner_->setError(
          EspBleError::BackendFailure,
          "failed to configure scan response data");
        return false;
      }
    }
  }

  backend->setAdvertisementType(
    connectable_ ? ADV_TYPE_IND
      : (scanResponseEnabled_ ? ADV_TYPE_SCAN_IND : ADV_TYPE_NONCONN_IND));
  backend->setAdvertisementChannelMap(
    static_cast<esp_ble_adv_channel_t>(
      channelMask_ == 0 ? EspBleAdvertisingChannelAll : channelMask_));
  const bool filterScanRequests =
    filterPolicy_ ==
      EspBleAdvertisingFilterPolicy::ScanRequestFromAcceptList ||
    filterPolicy_ == EspBleAdvertisingFilterPolicy::Both;
  const bool filterConnections =
    filterPolicy_ ==
      EspBleAdvertisingFilterPolicy::ConnectionFromAcceptList ||
    filterPolicy_ == EspBleAdvertisingFilterPolicy::Both;
  backend->setScanFilter(filterScanRequests, filterConnections);
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
  directedAdvertising_ = false;
  directedHighDutyCycle_ = false;
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
  directedAdvertising_ = false;
  directedHighDutyCycle_ = false;
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
  if (advertising_ && directedAdvertising_ && directedHighDutyCycle_ &&
      static_cast<uint32_t>(millis() - startedAtMs_) >= 1280)
  {
    // The controller stops high-duty directed advertising after at most
    // 1.28 seconds. Keep the public state in sync without issuing a second
    // stop request after the controller has already disabled it.
    advertising_ = false;
    directedAdvertising_ = false;
    directedHighDutyCycle_ = false;
    return;
  }
  if (advertising_ && durationMs_ != 0 &&
      static_cast<uint32_t>(millis() - startedAtMs_) >= durationMs_)
  {
    stop();
  }
}

EspBleScanner::EspBleScanner(EspBleBluedroid *owner) : owner_(owner) {}

EspBleScanner::~EspBleScanner()
{
  if (activeBleScanner.load(std::memory_order_acquire) == impl_)
  {
    activeBleScanner.store(nullptr, std::memory_order_release);
  }
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
  if (impl_->scanning.load(std::memory_order_acquire) && !stop())
  {
    return false;
  }
  flushPendingResults();
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->active = config.active;
    impl_->wantDuplicates = config.wantDuplicates;
    impl_->reportedAddresses.clear();
  }

  if (config.acceptListOnly)
  {
    prepareAcceptListOperation(owner_->connectionImpl_);
    if (esp_ble_gap_clear_whitelist() != ESP_OK ||
        !waitForAcceptListOperation(owner_->connectionImpl_))
    {
      owner_->setError(
        EspBleError::BackendFailure, "failed to clear the BLE accept list");
      return false;
    }
    for (size_t index = 0; index < owner_->acceptListCount_; ++index)
    {
      esp_bd_addr_t address = {};
      if (!espblebluedroid::internal::parseBleAddress(
            owner_->acceptList_[index].peerAddress.c_str(), address))
      {
        owner_->setError(
          EspBleError::InvalidArgument,
          "invalid BLE accept list address");
        return false;
      }
      prepareAcceptListOperation(owner_->connectionImpl_);
      if (esp_ble_gap_update_whitelist(
            true,
            address,
            acceptListBackendAddressType(
              owner_->acceptList_[index].peerAddressType)) != ESP_OK ||
          !waitForAcceptListOperation(owner_->connectionImpl_))
      {
        owner_->setError(
          EspBleError::BackendFailure,
          "failed to write the BLE accept list");
        return false;
      }
    }
  }

  esp_ble_scan_params_t parameters{};
  parameters.scan_type =
    config.active ? BLE_SCAN_TYPE_ACTIVE : BLE_SCAN_TYPE_PASSIVE;
  parameters.own_addr_type =
    owner_->activeOwnAddressType_ == EspBleOwnAddressType::Public
      ? BLE_ADDR_TYPE_PUBLIC
      : (owner_->activeOwnAddressType_ ==
             EspBleOwnAddressType::ResolvablePrivate
           ? BLE_ADDR_TYPE_RPA_RANDOM
           : BLE_ADDR_TYPE_RANDOM);
  parameters.scan_filter_policy = config.acceptListOnly
    ? BLE_SCAN_FILTER_ALLOW_ONLY_WLST
    : BLE_SCAN_FILTER_ALLOW_ALL;
  parameters.scan_interval = static_cast<uint16_t>(
    (static_cast<uint32_t>(config.intervalMilliseconds) * 8) / 5);
  parameters.scan_window = static_cast<uint16_t>(
    (static_cast<uint32_t>(config.windowMilliseconds) * 8) / 5);
  // Keep controller duplicate filtering disabled so an active advertisement
  // and its scan response can be merged before public duplicate filtering.
  parameters.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;

  activeBleScanner.store(impl_, std::memory_order_release);
  impl_->scanParamsCompleted.store(false, std::memory_order_release);
  impl_->scanParamsSucceeded.store(false, std::memory_order_release);
  if (esp_ble_gap_set_scan_params(&parameters) != ESP_OK)
  {
    owner_->setError(
      EspBleError::BackendFailure, "failed to configure BLE scan");
    return false;
  }
  const uint32_t parametersRequestedAt = millis();
  while (!impl_->scanParamsCompleted.load(std::memory_order_acquire) &&
         static_cast<uint32_t>(millis() - parametersRequestedAt) < 2000)
  {
    delay(1);
  }
  if (!impl_->scanParamsCompleted.load(std::memory_order_acquire) ||
      !impl_->scanParamsSucceeded.load(std::memory_order_acquire))
  {
    owner_->setError(
      EspBleError::BackendFailure, "BLE scan parameters were rejected");
    return false;
  }

  impl_->scanStartCompleted.store(false, std::memory_order_release);
  impl_->scanStartSucceeded.store(false, std::memory_order_release);
  if (esp_ble_gap_start_scanning(config.durationSeconds) != ESP_OK)
  {
    owner_->setError(EspBleError::BackendFailure, "failed to start BLE scan");
    return false;
  }
  const uint32_t startRequestedAt = millis();
  while (!impl_->scanStartCompleted.load(std::memory_order_acquire) &&
         static_cast<uint32_t>(millis() - startRequestedAt) < 2000)
  {
    delay(1);
  }
  if (!impl_->scanStartCompleted.load(std::memory_order_acquire) ||
      !impl_->scanStartSucceeded.load(std::memory_order_acquire))
  {
    owner_->setError(EspBleError::BackendFailure, "BLE scan did not start");
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
  if (impl_ == nullptr ||
      !impl_->scanning.load(std::memory_order_acquire))
  {
    owner_->clearError();
    return true;
  }
  impl_->scanStopCompleted.store(false, std::memory_order_release);
  if (esp_ble_gap_stop_scanning() != ESP_OK)
  {
    owner_->setError(EspBleError::BackendFailure, "failed to stop BLE scan");
    return false;
  }
  const uint32_t requestedAt = millis();
  while (!impl_->scanStopCompleted.load(std::memory_order_acquire) &&
         static_cast<uint32_t>(millis() - requestedAt) < 2000)
  {
    delay(1);
  }
  if (!impl_->scanStopCompleted.load(std::memory_order_acquire))
  {
    owner_->setError(EspBleError::BackendFailure, "BLE scan did not stop");
    return false;
  }
  flushPendingResults();
  owner_->clearError();
  return true;
}

bool EspBleScanner::isScanning() const
{
  return owner_->initialized() && impl_ != nullptr &&
    impl_->scanning.load(std::memory_order_acquire);
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
  return impl_->enqueue(result, true);
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
  impl_->reportedAddresses.clear();
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
      const uint32_t readyAtMs = impl_->queue[impl_->head].readyAtMs;
      if (readyAtMs != 0 &&
          static_cast<int32_t>(millis() - readyAtMs) < 0)
      {
        break;
      }
      result = std::move(impl_->queue[impl_->head].result);
      if (!impl_->wantDuplicates && !result.address.isEmpty())
      {
        impl_->reportedAddresses.insert(
          std::string(result.address.c_str()));
      }
      impl_->queue[impl_->head] = EspBleScannerImpl::QueueEntry();
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
  impl_->serverSecurity = EspBluedroidSppSecurity::None;
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
  impl_->connectSecurity = EspBluedroidSppSecurity::None;
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

void EspBluedroidSpp::onWriteCompleted(WriteCompletedCallback callback)
{
  const bool enabled = static_cast<bool>(callback);
  writeCompletedCallback_ = std::move(callback);
  if (impl_ != nullptr)
  {
    impl_->writeCompletionEventsEnabled.store(
      enabled, std::memory_order_release);
  }
}

void EspBluedroidSpp::onConnectionFailed(
  ConnectionFailureCallback callback)
{
  connectionFailedCallback_ = std::move(callback);
}

bool EspBluedroidSpp::connect(
  const char *address,
  uint32_t timeoutMilliseconds,
  EspBluedroidSppSecurity security)
{
  if (!owner_->initialized_)
  {
    owner_->setError(
      EspBleError::InvalidState, "Bluetooth stack is not initialized");
    return false;
  }
  if (
    security != EspBluedroidSppSecurity::None &&
    !owner_->activeClassicSecurity_.enabled)
  {
    owner_->setError(
      EspBleError::InvalidState,
      "enable Classic Security before requesting secure SPP");
    return false;
  }
#if !defined(CONFIG_BT_SPP_ENABLED)
  (void)address;
  (void)timeoutMilliseconds;
  (void)security;
  owner_->setError(EspBleError::Unsupported, "SPP is not enabled");
  return false;
#else
  if (
    !isValidBleAddress(address) || timeoutMilliseconds == 0 ||
    static_cast<uint8_t>(security) >
      static_cast<uint8_t>(
        EspBluedroidSppSecurity::AuthenticatedEncrypted))
  {
    owner_->setError(
      EspBleError::InvalidArgument,
      "SPP address, timeout, or Security mode is invalid");
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
    impl_->connectSecurity = security;
  }
  if (esp_spp_start_discovery(impl_->connectBackendAddress) != ESP_OK)
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->connecting = false;
    impl_->connectAddress = "";
    impl_->connectDeadlineMs = 0;
    impl_->connectSecurity = EspBluedroidSppSecurity::None;
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
  if (
    config.security != EspBluedroidSppSecurity::None &&
    !owner_->activeClassicSecurity_.enabled)
  {
    owner_->setError(
      EspBleError::InvalidState,
      "enable Classic Security before starting a secure SPP server");
    return false;
  }
#if !defined(CONFIG_BT_SPP_ENABLED)
  (void)config;
  owner_->setError(EspBleError::Unsupported, "SPP is not enabled");
  return false;
#else
  if (
    config.serviceName == nullptr || config.serviceName[0] == '\0' ||
    config.channel > ESP_SPP_MAX_SCN ||
    static_cast<uint8_t>(config.security) >
      static_cast<uint8_t>(
        EspBluedroidSppSecurity::AuthenticatedEncrypted))
  {
    owner_->setError(
      EspBleError::InvalidArgument,
      "invalid SPP service name, channel, or Security mode");
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
    impl_->serverSecurity = config.security;
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

EspBluedroidSppSerial::EspBluedroidSppSerial(
  EspBleBluedroid &bluetooth)
    : spp_(bluetooth.classic().spp())
{
}

EspBluedroidSppSessionId
EspBluedroidSppSerial::resolvedSessionId() const
{
  if (spp_.impl_ == nullptr) return 0;
  std::lock_guard<std::mutex> lock(spp_.impl_->mutex);
  if (spp_.impl_->backendHandle == 0) return 0;
  return spp_.impl_->activeSession.id;
}

bool EspBluedroidSppSerial::connected() const
{
  const EspBluedroidSppSessionId sessionId = resolvedSessionId();
  if (sessionId == 0) return false;
  EspBluedroidSppSession session;
  return spp_.session(sessionId, session);
}

EspBluedroidSppSerial::operator bool() const
{
  return connected();
}

EspBluedroidSppSessionId EspBluedroidSppSerial::sessionId() const
{
  return resolvedSessionId();
}

int EspBluedroidSppSerial::available()
{
  const EspBluedroidSppSessionId sessionId = resolvedSessionId();
  if (sessionId == 0) return 0;
  return static_cast<int>(spp_.available(sessionId));
}

int EspBluedroidSppSerial::peek()
{
  const EspBluedroidSppSessionId sessionId = resolvedSessionId();
  if (sessionId == 0) return -1;
  return spp_.peek(sessionId);
}

int EspBluedroidSppSerial::read()
{
  const EspBluedroidSppSessionId sessionId = resolvedSessionId();
  if (sessionId == 0) return -1;
  return spp_.read(sessionId);
}

int EspBluedroidSppSerial::availableForWrite()
{
  const EspBluedroidSppSessionId sessionId = resolvedSessionId();
  if (sessionId == 0) return 0;
  EspBluedroidSppSession session;
  if (!spp_.session(sessionId, session)) return 0;
  const size_t pending = spp_.pendingWriteCount(sessionId);
  if (pending >= EspBluedroidSpp::WriteQueueCapacity) return 0;
  return static_cast<int>(
    (EspBluedroidSpp::WriteQueueCapacity - pending) *
    EspBluedroidSpp::MaximumWriteSize);
}

void EspBluedroidSppSerial::flush()
{
  const EspBluedroidSppSessionId sessionId = resolvedSessionId();
  if (sessionId == 0) return;
  while (
    resolvedSessionId() == sessionId &&
    spp_.pendingWriteCount(sessionId) != 0)
  {
    delay(1);
  }
}

size_t EspBluedroidSppSerial::write(uint8_t value)
{
  return write(&value, 1);
}

size_t EspBluedroidSppSerial::write(
  const uint8_t *data,
  size_t length)
{
  if (data == nullptr || length == 0)
  {
    if (length != 0) setWriteError();
    return 0;
  }
  const EspBluedroidSppSessionId sessionId = resolvedSessionId();
  EspBluedroidSppSession session;
  if (sessionId == 0 || !spp_.session(sessionId, session))
  {
    setWriteError();
    return 0;
  }
  size_t written = 0;
  while (written < length)
  {
    const size_t remaining = length - written;
    const size_t chunk =
      std::min(remaining, EspBluedroidSpp::MaximumWriteSize);
    if (!spp_.write(sessionId, data + written, chunk))
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
      event.type == EspBluedroidSppImpl::EventType::WriteCompleted &&
      writeCompletedCallback_)
    {
      writeCompletedCallback_(event.writeResult);
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
    : owner_(owner), inquiry_(owner), spp_(owner),
      a2dpSink_(owner), a2dpSource_(owner),
      avrcpController_(owner), avrcpTarget_(owner),
      hfpHandsFree_(owner), hfpAudioGateway_(owner)
{
}

EspBluedroidClassic::~EspBluedroidClassic()
{
  end();
  delete impl_;
}

EspBluedroidClassicInquiry &EspBluedroidClassic::inquiry()
{
  return inquiry_;
}

EspBluedroidSpp &EspBluedroidClassic::spp()
{
  return spp_;
}

EspBluedroidA2dpSink &EspBluedroidClassic::a2dpSink()
{
  return a2dpSink_;
}

EspBluedroidA2dpSource &EspBluedroidClassic::a2dpSource()
{
  return a2dpSource_;
}

EspBluedroidAvrcpController &EspBluedroidClassic::avrcpController()
{
  return avrcpController_;
}

EspBluedroidAvrcpTarget &EspBluedroidClassic::avrcpTarget()
{
  return avrcpTarget_;
}

EspBluedroidHfpHandsFree &EspBluedroidClassic::hfpHandsFree()
{
  return hfpHandsFree_;
}

EspBluedroidHfpAudioGateway &EspBluedroidClassic::hfpAudioGateway()
{
  return hfpAudioGateway_;
}

EspBluedroidClassicProfileSupport EspBluedroidClassic::profileSupport(
  EspBluedroidClassicProfile profile) const
{
  EspBluedroidClassicProfileSupport result;
  result.profile = profile;

#if !defined(CONFIG_BT_CLASSIC_ENABLED)
  result.status = EspBluedroidClassicProfileStatus::CoreDisabled;
  result.reason =
    "Bluetooth Classic is not enabled for the selected SoC/build";
  return result;
#else
  const auto supported = [&result](const char *reason) {
    result.status = EspBluedroidClassicProfileStatus::Supported;
    result.coreAvailable = true;
    result.implemented = true;
    result.reason = reason;
  };
  const auto notImplemented = [&result](const char *reason) {
    result.status =
      EspBluedroidClassicProfileStatus::LibraryNotImplemented;
    result.coreAvailable = true;
    result.implemented = false;
    result.reason = reason;
  };
  const auto coreDisabled = [&result](const char *reason) {
    result.status = EspBluedroidClassicProfileStatus::CoreDisabled;
    result.coreAvailable = false;
    result.implemented = false;
    result.reason = reason;
  };
  const auto coreApiUnavailable = [&result](const char *reason) {
    result.status =
      EspBluedroidClassicProfileStatus::CoreApiUnavailable;
    result.coreAvailable = false;
    result.implemented = false;
    result.reason = reason;
  };

  switch (profile)
  {
    case EspBluedroidClassicProfile::Gap:
      supported("Classic GAP, Inquiry, Security, and bond APIs are available");
      break;
    case EspBluedroidClassicProfile::Spp:
#if defined(CONFIG_BT_SPP_ENABLED)
      supported("SPP Server and Client APIs are available");
#else
      coreDisabled("CONFIG_BT_SPP_ENABLED is disabled by the Core build");
#endif
      break;
    case EspBluedroidClassicProfile::A2dpSink:
    case EspBluedroidClassicProfile::A2dpSource:
#if defined(CONFIG_BT_A2DP_ENABLE)
      supported(
        "A2DP Sink and Source SBC/PCM APIs are available; only one A2DP role may be active");
#else
      coreDisabled("CONFIG_BT_A2DP_ENABLE is disabled by the Core build");
#endif
      break;
    case EspBluedroidClassicProfile::AvrcpController:
    case EspBluedroidClassicProfile::AvrcpTarget:
#if defined(CONFIG_BT_AVRCP_ENABLED)
      supported(
        "AVRCP Controller and Target passthrough and absolute-volume APIs are available");
#else
      coreDisabled("CONFIG_BT_AVRCP_ENABLED is disabled by the Core build");
#endif
      break;
    case EspBluedroidClassicProfile::HidDevice:
    case EspBluedroidClassicProfile::HidHost:
#if defined(CONFIG_BT_HID_ENABLED)
      notImplemented(
        "Classic HID is enabled; EspBleBluedroid API is not implemented yet");
#else
      coreDisabled("CONFIG_BT_HID_ENABLED is disabled by the Core build");
#endif
      break;
    case EspBluedroidClassicProfile::HfpHandsFree:
#if defined(CONFIG_BT_HFP_CLIENT_ENABLE)
      notImplemented(
        "HFP Hands-Free SLC, SCO, and built-in-codec PCM APIs are available; call control is still being implemented");
#else
      coreDisabled(
        "CONFIG_BT_HFP_CLIENT_ENABLE is disabled by the Core build");
#endif
      break;
    case EspBluedroidClassicProfile::HfpAudioGateway:
#if defined(CONFIG_BT_HFP_AG_ENABLE)
      notImplemented(
        "HFP Audio Gateway SLC, SCO, and built-in-codec PCM APIs are available; call control is still being implemented");
#else
      coreDisabled("CONFIG_BT_HFP_AG_ENABLE is disabled by the Core build");
#endif
      break;
    case EspBluedroidClassicProfile::PbapClient:
#if defined(CONFIG_BT_PBAC_ENABLED)
      notImplemented(
        "CONFIG_BT_PBAC_ENABLED is enabled; EspBleBluedroid API is not implemented yet");
#else
      coreDisabled("CONFIG_BT_PBAC_ENABLED is disabled by the Core build");
#endif
      break;
    case EspBluedroidClassicProfile::Hsp:
      coreApiUnavailable(
        "the Core does not expose a dedicated supported HSP profile API");
      break;
    case EspBluedroidClassicProfile::Pan:
      coreApiUnavailable(
        "the Core does not expose a supported PAN/BNEP profile API");
      break;
    case EspBluedroidClassicProfile::PbapServer:
      coreApiUnavailable(
        "the Core does not expose a supported PBAP Server profile API");
      break;
    case EspBluedroidClassicProfile::Map:
      coreApiUnavailable(
        "the Core does not expose a supported MAP profile API");
      break;
    case EspBluedroidClassicProfile::Opp:
      coreApiUnavailable(
        "the Core does not expose a supported OPP profile API");
      break;
    case EspBluedroidClassicProfile::Ftp:
      coreApiUnavailable(
        "the Core does not expose a supported FTP profile API");
      break;
    case EspBluedroidClassicProfile::Dun:
      coreApiUnavailable(
        "the Core does not expose a supported DUN profile API");
      break;
    case EspBluedroidClassicProfile::Sap:
      coreApiUnavailable(
        "the Core does not expose a supported SAP profile API");
      break;
    case EspBluedroidClassicProfile::Midi:
      result.status = EspBluedroidClassicProfileStatus::NoStandardProfile;
      result.reason =
        "Bluetooth Classic has no standard MIDI profile; proprietary SPP MIDI is out of scope";
      break;
  }
  return result;
#endif
}

void EspBluedroidClassic::onSecurityChanged(
  SecurityChangedCallback callback)
{
  securityChangedCallback_ = std::move(callback);
}

void EspBluedroidClassic::onNumericComparisonRequested(
  NumericComparisonCallback callback)
{
  numericComparisonCallback_ = std::move(callback);
  if (impl_ != nullptr)
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->numericComparisonCallbackConfigured =
      static_cast<bool>(numericComparisonCallback_);
  }
}

void EspBluedroidClassic::onPasskeyDisplayed(
  PasskeyDisplayedCallback callback)
{
  passkeyDisplayedCallback_ = std::move(callback);
}

void EspBluedroidClassic::onPasskeyRequested(
  PasskeyRequestedCallback callback)
{
  passkeyRequestedCallback_ = std::move(callback);
  if (impl_ != nullptr)
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->passkeyRequestedCallbackConfigured =
      static_cast<bool>(passkeyRequestedCallback_);
  }
}

bool EspBluedroidClassic::confirmNumericComparison(
  const char *peerAddress,
  bool accept)
{
#if !defined(CONFIG_BT_CLASSIC_ENABLED)
  (void)peerAddress;
  (void)accept;
  owner_->setError(
    EspBleError::Unsupported, "Classic Bluetooth is not enabled");
  return false;
#else
  if (
    impl_ == nullptr || !isValidBleAddress(peerAddress))
  {
    owner_->setError(
      EspBleError::InvalidArgument,
      "a canonical Classic peer address is required");
    return false;
  }
  esp_bd_addr_t address = {};
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (
      !impl_->numericComparisonPending ||
      !impl_->numericComparisonAddress.equalsIgnoreCase(peerAddress))
    {
      owner_->setError(
        EspBleError::NotFound,
        "Classic Numeric Comparison request was not found");
      return false;
    }
    memcpy(
      address, impl_->numericComparisonBackendAddress,
      sizeof(address));
  }
  if (esp_bt_gap_ssp_confirm_reply(address, accept) != ESP_OK)
  {
    owner_->setError(
      EspBleError::BackendFailure,
      "failed to reply to Classic Numeric Comparison");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->numericComparisonPending = false;
    impl_->numericComparisonAddress = "";
    memset(
      impl_->numericComparisonBackendAddress, 0,
      sizeof(impl_->numericComparisonBackendAddress));
    impl_->numericComparisonDeadlineMs = 0;
  }
  owner_->clearError();
  return true;
#endif
}

bool EspBluedroidClassic::providePasskey(
  const char *peerAddress,
  uint32_t passkey)
{
#if !defined(CONFIG_BT_CLASSIC_ENABLED)
  (void)peerAddress;
  (void)passkey;
  owner_->setError(
    EspBleError::Unsupported, "Classic Bluetooth is not enabled");
  return false;
#else
  if (!isValidBleAddress(peerAddress) || passkey > 999999)
  {
    owner_->setError(
      EspBleError::InvalidArgument,
      "a canonical Classic peer address and six-digit passkey are required");
    return false;
  }
  if (impl_ == nullptr)
  {
    owner_->setError(
      EspBleError::InvalidState,
      "Classic Bluetooth stack is not initialized");
    return false;
  }
  esp_bd_addr_t address = {};
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (
      !impl_->passkeyPending ||
      !impl_->passkeyAddress.equalsIgnoreCase(peerAddress))
    {
      owner_->setError(
        EspBleError::NotFound,
        "Classic Passkey Entry request was not found");
      return false;
    }
    memcpy(address, impl_->passkeyBackendAddress, sizeof(address));
  }
  if (esp_bt_gap_ssp_passkey_reply(address, true, passkey) != ESP_OK)
  {
    owner_->setError(
      EspBleError::BackendFailure,
      "failed to reply to Classic Passkey Entry");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->passkeyPending = false;
    impl_->passkeyAddress = "";
    memset(
      impl_->passkeyBackendAddress, 0,
      sizeof(impl_->passkeyBackendAddress));
    impl_->passkeyDeadlineMs = 0;
  }
  owner_->clearError();
  return true;
#endif
}

size_t EspBluedroidClassic::bondCount() const
{
#if !defined(CONFIG_BT_CLASSIC_ENABLED)
  return 0;
#else
  if (!owner_->initialized()) return 0;
  const int count = esp_bt_gap_get_bond_device_num();
  return count > 0 ? static_cast<size_t>(count) : 0;
#endif
}

bool EspBluedroidClassic::bond(
  size_t index,
  EspBluedroidClassicBond &bond) const
{
#if !defined(CONFIG_BT_CLASSIC_ENABLED)
  (void)index;
  (void)bond;
  return false;
#else
  if (!owner_->initialized()) return false;
  const int count = esp_bt_gap_get_bond_device_num();
  if (count <= 0 || index >= static_cast<size_t>(count)) return false;
  esp_bd_addr_t *bonds = new (std::nothrow) esp_bd_addr_t[count];
  if (bonds == nullptr) return false;
  int listed = count;
  const bool success =
    esp_bt_gap_get_bond_device_list(&listed, bonds) == ESP_OK &&
    index < static_cast<size_t>(listed);
  if (success)
  {
    bond.peerAddress = classicAddress(bonds[index]);
  }
  delete[] bonds;
  return success;
#endif
}

bool EspBluedroidClassic::deleteBond(
  const EspBluedroidClassicBond &bond)
{
#if !defined(CONFIG_BT_CLASSIC_ENABLED)
  (void)bond;
  owner_->setError(
    EspBleError::Unsupported, "Classic Bluetooth is not enabled");
  return false;
#else
  if (!owner_->initialized() || spp_.impl_ == nullptr)
  {
    owner_->setError(
      EspBleError::InvalidState,
      "Classic Bluetooth stack is not initialized");
    return false;
  }
  if (!isValidBleAddress(bond.peerAddress.c_str()))
  {
    owner_->setError(
      EspBleError::InvalidArgument,
      "a canonical Classic peer address is required");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(spp_.impl_->mutex);
    if (spp_.impl_->backendHandle != 0 || spp_.impl_->connecting)
    {
      owner_->setError(
        EspBleError::InvalidState,
        "disconnect SPP before deleting a Classic bond");
      return false;
    }
  }
  const int count = esp_bt_gap_get_bond_device_num();
  esp_bd_addr_t *bonds =
    count > 0 ? new (std::nothrow) esp_bd_addr_t[count] : nullptr;
  if (count > 0 && bonds == nullptr)
  {
    owner_->setError(
      EspBleError::ResourceExhausted,
      "failed to allocate Classic bond list");
    return false;
  }
  int listed = count;
  if (
    count > 0 &&
    esp_bt_gap_get_bond_device_list(&listed, bonds) != ESP_OK)
  {
    delete[] bonds;
    owner_->setError(
      EspBleError::BackendFailure,
      "failed to enumerate Classic bonds");
    return false;
  }
  for (int index = 0; index < listed; ++index)
  {
    if (classicAddress(bonds[index]).equalsIgnoreCase(bond.peerAddress))
    {
      const esp_err_t result =
        esp_bt_gap_remove_bond_device(bonds[index]);
      delete[] bonds;
      if (result != ESP_OK)
      {
        owner_->setError(
          EspBleError::BackendFailure,
          "failed to delete Classic bond");
        return false;
      }
      const uint32_t startedAt = millis();
      while (
        esp_bt_gap_get_bond_device_num() >= count &&
        static_cast<uint32_t>(millis() - startedAt) < 2000)
      {
        delay(10);
      }
      if (esp_bt_gap_get_bond_device_num() >= count)
      {
        owner_->setError(
          EspBleError::Timeout,
          "timed out waiting for Classic bond deletion");
        return false;
      }
      owner_->clearError();
      return true;
    }
  }
  delete[] bonds;
  owner_->setError(
    EspBleError::NotFound, "Classic bond was not found");
  return false;
#endif
}

bool EspBluedroidClassic::deleteAllBonds()
{
#if !defined(CONFIG_BT_CLASSIC_ENABLED)
  owner_->setError(
    EspBleError::Unsupported, "Classic Bluetooth is not enabled");
  return false;
#else
  if (!owner_->initialized() || spp_.impl_ == nullptr)
  {
    owner_->setError(
      EspBleError::InvalidState,
      "Classic Bluetooth stack is not initialized");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(spp_.impl_->mutex);
    if (spp_.impl_->backendHandle != 0 || spp_.impl_->connecting)
    {
      owner_->setError(
        EspBleError::InvalidState,
        "disconnect SPP before deleting Classic bonds");
      return false;
    }
  }
  const int count = esp_bt_gap_get_bond_device_num();
  if (count <= 0)
  {
    owner_->clearError();
    return true;
  }
  esp_bd_addr_t *bonds = new (std::nothrow) esp_bd_addr_t[count];
  if (bonds == nullptr)
  {
    owner_->setError(
      EspBleError::ResourceExhausted,
      "failed to allocate Classic bond list");
    return false;
  }
  int listed = count;
  if (esp_bt_gap_get_bond_device_list(&listed, bonds) != ESP_OK)
  {
    delete[] bonds;
    owner_->setError(
      EspBleError::BackendFailure,
      "failed to enumerate Classic bonds");
    return false;
  }
  for (int index = 0; index < listed; ++index)
  {
    if (esp_bt_gap_remove_bond_device(bonds[index]) != ESP_OK)
    {
      delete[] bonds;
      owner_->setError(
        EspBleError::BackendFailure,
        "failed to delete all Classic bonds");
      return false;
    }
  }
  delete[] bonds;
  const uint32_t startedAt = millis();
  while (
    esp_bt_gap_get_bond_device_num() != 0 &&
    static_cast<uint32_t>(millis() - startedAt) < 2000)
  {
    delay(10);
  }
  if (esp_bt_gap_get_bond_device_num() != 0)
  {
    owner_->setError(
      EspBleError::Timeout,
      "timed out waiting for Classic bond deletion");
    return false;
  }
  owner_->clearError();
  return true;
#endif
}

bool EspBluedroidClassic::begin(
  const char *deviceName,
  const EspBluedroidClassicSecurityConfig &security)
{
  if (impl_ == nullptr)
  {
    impl_ = new (std::nothrow) EspBluedroidClassicImpl();
    if (impl_ == nullptr)
    {
      owner_->setError(
        EspBleError::ResourceExhausted,
        "failed to allocate Classic Security state");
      return false;
    }
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->security = security;
    impl_->numericComparisonCallbackConfigured =
      static_cast<bool>(numericComparisonCallback_);
    impl_->passkeyRequestedCallbackConfigured =
      static_cast<bool>(passkeyRequestedCallback_);
    impl_->eventHead = 0;
    impl_->eventCount = 0;
    impl_->dropped = 0;
    impl_->numericComparisonPending = false;
    impl_->numericComparisonAddress = "";
    memset(
      impl_->numericComparisonBackendAddress, 0,
      sizeof(impl_->numericComparisonBackendAddress));
    impl_->numericComparisonDeadlineMs = 0;
    impl_->passkeyPending = false;
    impl_->passkeyAddress = "";
    memset(
      impl_->passkeyBackendAddress, 0,
      sizeof(impl_->passkeyBackendAddress));
    impl_->passkeyDeadlineMs = 0;
  }
#if defined(CONFIG_BT_CLASSIC_ENABLED)
  activeClassic.store(impl_, std::memory_order_release);
#endif
  if (!inquiry_.begin(deviceName))
  {
#if defined(CONFIG_BT_CLASSIC_ENABLED)
    activeClassic.store(nullptr, std::memory_order_release);
#endif
    return false;
  }
#if defined(CONFIG_BT_CLASSIC_ENABLED)
  esp_bt_io_cap_t capability = ESP_BT_IO_CAP_NONE;
  if (security.ioCapability ==
      EspBluedroidClassicSecurityIoCapability::DisplayOnly)
  {
    capability = ESP_BT_IO_CAP_OUT;
  }
  else if (security.ioCapability ==
           EspBluedroidClassicSecurityIoCapability::KeyboardOnly)
  {
    capability = ESP_BT_IO_CAP_IN;
  }
  else if (
    security.ioCapability ==
      EspBluedroidClassicSecurityIoCapability::DisplayYesNo)
  {
    capability = ESP_BT_IO_CAP_IO;
  }
  if (esp_bt_gap_set_security_param(
        ESP_BT_SP_IOCAP_MODE,
        &capability, sizeof(capability)) != ESP_OK)
  {
    activeClassic.store(nullptr, std::memory_order_release);
    inquiry_.end();
    owner_->setError(
      EspBleError::BackendFailure,
      "failed to configure Classic Security I/O capability");
    return false;
  }
#endif
  if (!spp_.begin())
  {
#if defined(CONFIG_BT_CLASSIC_ENABLED)
    activeClassic.store(nullptr, std::memory_order_release);
#endif
    inquiry_.end();
    return false;
  }
  return true;
}

void EspBluedroidClassic::end()
{
#if defined(CONFIG_BT_CLASSIC_ENABLED)
  activeClassic.store(nullptr, std::memory_order_release);
  esp_bd_addr_t pendingAddress = {};
  esp_bd_addr_t pendingPasskeyAddress = {};
  bool pending = false;
  bool passkeyPending = false;
  if (impl_ != nullptr)
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    pending = impl_->numericComparisonPending;
    memcpy(
      pendingAddress, impl_->numericComparisonBackendAddress,
      sizeof(pendingAddress));
    passkeyPending = impl_->passkeyPending;
    memcpy(
      pendingPasskeyAddress, impl_->passkeyBackendAddress,
      sizeof(pendingPasskeyAddress));
  }
  if (pending)
  {
    esp_bt_gap_ssp_confirm_reply(pendingAddress, false);
  }
  if (passkeyPending)
  {
    esp_bt_gap_ssp_passkey_reply(pendingPasskeyAddress, false, 0);
  }
#endif
  hfpAudioGateway_.end();
  hfpHandsFree_.end();
  avrcpTarget_.end();
  avrcpController_.end();
  a2dpSource_.end();
  a2dpSink_.end();
  spp_.end();
  inquiry_.end();
  if (impl_ != nullptr)
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->eventHead = 0;
    impl_->eventCount = 0;
    impl_->dropped = 0;
    impl_->security = EspBluedroidClassicSecurityConfig();
    impl_->numericComparisonCallbackConfigured = false;
    impl_->passkeyRequestedCallbackConfigured = false;
    impl_->numericComparisonPending = false;
    impl_->numericComparisonAddress = "";
    memset(
      impl_->numericComparisonBackendAddress, 0,
      sizeof(impl_->numericComparisonBackendAddress));
    impl_->numericComparisonDeadlineMs = 0;
    impl_->passkeyPending = false;
    impl_->passkeyAddress = "";
    memset(
      impl_->passkeyBackendAddress, 0,
      sizeof(impl_->passkeyBackendAddress));
    impl_->passkeyDeadlineMs = 0;
  }
}

void EspBluedroidClassic::update()
{
  if (impl_ != nullptr)
  {
#if defined(CONFIG_BT_CLASSIC_ENABLED)
    esp_bd_addr_t timedOutAddress = {};
    esp_bd_addr_t timedOutPasskeyAddress = {};
    bool timedOut = false;
    bool passkeyTimedOut = false;
    {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      timedOut =
        impl_->numericComparisonPending &&
        static_cast<int32_t>(
          millis() - impl_->numericComparisonDeadlineMs) >= 0;
      if (timedOut)
      {
        memcpy(
          timedOutAddress, impl_->numericComparisonBackendAddress,
          sizeof(timedOutAddress));
        impl_->numericComparisonPending = false;
        impl_->numericComparisonAddress = "";
        memset(
          impl_->numericComparisonBackendAddress, 0,
          sizeof(impl_->numericComparisonBackendAddress));
        impl_->numericComparisonDeadlineMs = 0;
      }
      passkeyTimedOut =
        impl_->passkeyPending &&
        static_cast<int32_t>(
          millis() - impl_->passkeyDeadlineMs) >= 0;
      if (passkeyTimedOut)
      {
        memcpy(
          timedOutPasskeyAddress, impl_->passkeyBackendAddress,
          sizeof(timedOutPasskeyAddress));
        impl_->passkeyPending = false;
        impl_->passkeyAddress = "";
        memset(
          impl_->passkeyBackendAddress, 0,
          sizeof(impl_->passkeyBackendAddress));
        impl_->passkeyDeadlineMs = 0;
      }
    }
    if (timedOut)
    {
      esp_bt_gap_ssp_confirm_reply(timedOutAddress, false);
    }
    if (passkeyTimedOut)
    {
      esp_bt_gap_ssp_passkey_reply(timedOutPasskeyAddress, false, 0);
    }
#endif
    while (true)
    {
      EspBluedroidClassicImpl::Event event;
      {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->eventCount == 0) break;
        event = std::move(impl_->events[impl_->eventHead]);
        impl_->eventHead =
          (impl_->eventHead + 1) %
          ClassicSecurityEventQueueCapacity;
        --impl_->eventCount;
      }
      if (
        event.type ==
          EspBluedroidClassicImpl::EventType::SecurityChanged &&
        securityChangedCallback_)
      {
        securityChangedCallback_(event.securityChanged);
      }
      else if (
        event.type ==
          EspBluedroidClassicImpl::EventType::NumericComparison &&
        numericComparisonCallback_)
      {
        numericComparisonCallback_(event.numericComparison);
      }
      else if (
        event.type ==
          EspBluedroidClassicImpl::EventType::PasskeyDisplayed &&
        passkeyDisplayedCallback_)
      {
        passkeyDisplayedCallback_(event.passkeyDisplayed);
      }
      else if (
        event.type ==
          EspBluedroidClassicImpl::EventType::PasskeyRequested &&
        passkeyRequestedCallback_)
      {
        passkeyRequestedCallback_(event.passkeyRequested);
      }
    }
  }
  inquiry_.update();
  spp_.update();
  a2dpSink_.update();
  a2dpSource_.update();
  avrcpController_.update();
  avrcpTarget_.update();
  hfpHandsFree_.update();
  hfpAudioGateway_.update();
}

EspBleBluedroid::EspBleBluedroid()
    : advertising_(this), scanner_(this), gattServer_(this), classic_(this)
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
        activeOwnAddressType_ != config.ownAddressType ||
        !sameSecurityConfig(activeSecurity_, config.security) ||
        !sameClassicSecurityConfig(
          activeClassicSecurity_, config.classicSecurity))
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
  if (static_cast<uint8_t>(config.ownAddressType) >
      static_cast<uint8_t>(EspBleOwnAddressType::ResolvablePrivate))
  {
    setError(EspBleError::InvalidArgument, "unsupported own address type");
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
  if (
    !config.classicSecurity.enabled &&
    config.classicSecurity.ioCapability !=
      EspBluedroidClassicSecurityIoCapability::None)
  {
    setError(
      EspBleError::InvalidArgument,
      "enable Classic Security before configuring its I/O capability");
    return false;
  }
  if (
    static_cast<uint8_t>(config.classicSecurity.ioCapability) >
      static_cast<uint8_t>(
        EspBluedroidClassicSecurityIoCapability::DisplayYesNo))
  {
    setError(
      EspBleError::InvalidArgument,
      "unsupported Classic Security I/O capability");
    return false;
  }
  if (config.classicSecurity.responseTimeoutMilliseconds == 0)
  {
    setError(
      EspBleError::InvalidArgument,
      "Classic Security response timeout must be nonzero");
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
  EspBleConnectionImpl::customGattcOwner = connectionImpl_;
  EspBleConnectionImpl::customGapOwner = connectionImpl_;
  connectionImpl_->preferredMtu = config.preferredMtu;
  BLEDevice::setCustomGattcHandler(
    &EspBleConnectionImpl::customGattcHandler);
  BLEDevice::setCustomGapHandler(
    &EspBleConnectionImpl::customGapHandler);
  if (BLEDevice::setMTU(config.preferredMtu) != ESP_OK)
  {
    BLEDevice::setCustomGattcHandler(nullptr);
    BLEDevice::setCustomGapHandler(nullptr);
    EspBleConnectionImpl::customGattcOwner = nullptr;
    EspBleConnectionImpl::customGapOwner = nullptr;
    BLEDevice::deinit(false);
    delete connectionImpl_;
    connectionImpl_ = nullptr;
    setError(EspBleError::BackendFailure, "failed to set preferred MTU");
    return false;
  }
  activeRandomAddressPresent_ = false;
  memset(activeRandomAddress_, 0, sizeof(activeRandomAddress_));
  if (config.ownAddressType != EspBleOwnAddressType::Public)
  {
    esp_bd_addr_t randomAddress;
    if (esp_ble_gap_addr_create_static(randomAddress) != ESP_OK)
    {
      BLEDevice::setCustomGattcHandler(nullptr);
      BLEDevice::setCustomGapHandler(nullptr);
      EspBleConnectionImpl::customGattcOwner = nullptr;
      EspBleConnectionImpl::customGapOwner = nullptr;
      BLEDevice::deinit(false);
      delete connectionImpl_;
      connectionImpl_ = nullptr;
      setError(
        EspBleError::BackendFailure,
        "failed to configure the BLE private address");
      return false;
    }
    memcpy(
      activeRandomAddress_, randomAddress,
      sizeof(activeRandomAddress_));
    activeRandomAddressPresent_ = true;
  }
  if (!classic_.begin(deviceName, config.classicSecurity))
  {
    BLEDevice::setCustomGattcHandler(nullptr);
    BLEDevice::setCustomGapHandler(nullptr);
    EspBleConnectionImpl::customGattcOwner = nullptr;
    EspBleConnectionImpl::customGapOwner = nullptr;
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
      BLEDevice::setCustomGattcHandler(nullptr);
      BLEDevice::setCustomGapHandler(nullptr);
      EspBleConnectionImpl::customGattcOwner = nullptr;
      EspBleConnectionImpl::customGapOwner = nullptr;
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

  if (!gattServer_.realize())
  {
    classic_.end();
    BLEDevice::setSecurityCallbacks(nullptr);
    BLEDevice::setCustomGattcHandler(nullptr);
    BLEDevice::setCustomGapHandler(nullptr);
    EspBleConnectionImpl::customGattcOwner = nullptr;
    EspBleConnectionImpl::customGapOwner = nullptr;
    BLEDevice::deinit(false);
    gattServer_.resetBackend();
    delete connectionImpl_;
    connectionImpl_ = nullptr;
    if (lastError_ == EspBleError::None)
      setError(EspBleError::BackendFailure, "failed to realize GATT Server");
    return false;
  }

  activeDeviceName_ = deviceName;
  activePreferredMtu_ = config.preferredMtu;
  activeOwnAddressType_ = config.ownAddressType;
  activeSecurity_ = config.security;
  activeClassicSecurity_ = config.classicSecurity;
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
    esp_ble_gap_stop_scanning();
  }
  activeBleScanner.store(nullptr, std::memory_order_release);
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
      if (connectionImpl_->gattDirectDiscovery)
      {
        delete connectionImpl_->gattDiscoveryDatabase;
        connectionImpl_->gattDiscoveryDatabase = nullptr;
        connectionImpl_->gattDirectDiscovery = false;
        connectionImpl_->gattOperating = false;
      }
      if (connectionImpl_->gattDirectCharacteristicRead)
      {
        connectionImpl_->gattDirectCharacteristicRead = false;
        connectionImpl_->gattOperating = false;
      }
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
  gattServer_.resetBackend();
  BLEDevice::setCustomGattcHandler(nullptr);
  BLEDevice::setCustomGapHandler(nullptr);
  EspBleConnectionImpl::customGattcOwner = nullptr;
  EspBleConnectionImpl::customGapOwner = nullptr;
  initialized_ = false;
  activeOwnAddressType_ = EspBleOwnAddressType::Public;
  activeRandomAddressPresent_ = false;
  memset(activeRandomAddress_, 0, sizeof(activeRandomAddress_));
  activeClassicSecurity_ = EspBluedroidClassicSecurityConfig();
  delete connectionImpl_;
  connectionImpl_ = nullptr;
}

void EspBleBluedroid::update()
{
  advertising_.update();
  gattServer_.update();
  if (connectionImpl_ != nullptr)
  {
    connectionImpl_->requestPreferredMtu();
    connectionImpl_->requestPendingDisconnect();
  }
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

String EspBleBluedroid::localAddress() const
{
  if (!initialized_) return String();
  esp_bd_addr_t address;
  // ESP-IDF's public getter returns the identity address while a controller-
  // generated RPA is on air. Returning it as the current address would be
  // misleading, so report the address as unavailable in this mode.
  if (activeOwnAddressType_ == EspBleOwnAddressType::ResolvablePrivate)
    return String();
  uint8_t addressType = 0;
  if (esp_ble_gap_get_local_used_addr(address, &addressType) != ESP_OK)
  {
    return String();
  }
  return String(
    espblebluedroid::internal::formatBleAddress(address).c_str());
}

EspBleAddressType EspBleBluedroid::localAddressType() const
{
  return activeOwnAddressType_ == EspBleOwnAddressType::Public
    ? EspBleAddressType::Public
    : EspBleAddressType::Random;
}

bool EspBleBluedroid::setTxPower(int8_t dBm)
{
  if (!initialized_)
  {
    setError(EspBleError::InvalidState, "BLE stack is not initialized");
    return false;
  }
  const BleTxPowerLevel *nearest = &BleTxPowerLevels[0];
  for (const BleTxPowerLevel &candidate : BleTxPowerLevels)
  {
    if (abs(static_cast<int>(candidate.dBm) - dBm) <
        abs(static_cast<int>(nearest->dBm) - dBm))
    {
      nearest = &candidate;
    }
  }
  if (esp_ble_tx_power_set(
        ESP_BLE_PWR_TYPE_DEFAULT, nearest->backend) != ESP_OK ||
      esp_ble_tx_power_set(
        ESP_BLE_PWR_TYPE_ADV, nearest->backend) != ESP_OK ||
      esp_ble_tx_power_set(
        ESP_BLE_PWR_TYPE_SCAN, nearest->backend) != ESP_OK)
  {
    setError(EspBleError::BackendFailure, "failed to set BLE transmit power");
    return false;
  }
  clearError();
  return true;
}

int8_t EspBleBluedroid::txPower() const
{
  if (!initialized_) return INT8_MIN;
  return bleTxPowerDbm(esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_ADV));
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

EspBleGattServer &EspBleBluedroid::gattServer()
{
  return gattServer_;
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

bool EspBleBluedroid::injectNotificationForTest(
  const EspBleGattNotification &notification)
{
  if (!initialized_ || connectionImpl_ == nullptr) return false;
  std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
  EspBleConnectionImpl::Event event;
  event.type = EspBleConnectionImpl::EventType::Notification;
  event.notification = notification;
  return connectionImpl_->pushEventLocked(event);
}

bool EspBleBluedroid::injectGattResultForTest(
  const EspBleGattResult &result)
{
  if (!initialized_ || connectionImpl_ == nullptr) return false;
  std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
  EspBleConnectionImpl::Event event;
  event.type = EspBleConnectionImpl::EventType::GattResult;
  event.gattResult = result;
  return connectionImpl_->pushEventLocked(event);
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
    connectionImpl_->pendingMtuPresent = false;
    connectionImpl_->mtuRequestPending = false;
    connectionImpl_->mtuRequestInFlight = false;
    connectionImpl_->disconnectPending = false;
    connectionImpl_->pendingConnectionParametersPresent = false;
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
    if (connectionImpl_->mtuRequestInFlight)
    {
      connectionImpl_->disconnectPending = true;
      clearError();
      return true;
    }
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

bool EspBleBluedroid::updateConnectionParameters(
  EspBleConnectionId connectionId,
  uint16_t minInterval,
  uint16_t maxInterval,
  uint16_t latency,
  uint16_t supervisionTimeout)
{
  if (!initialized_ || connectionImpl_ == nullptr)
  {
    setError(EspBleError::InvalidState, "BLE stack is not initialized");
    return false;
  }
  if (minInterval > maxInterval)
  {
    setError(
      EspBleError::InvalidArgument,
      "minInterval must not exceed maxInterval");
    return false;
  }
  BLEClient *client;
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
  if (client == nullptr ||
      !client->updateConnParams(
        minInterval, maxInterval, latency, supervisionTimeout))
  {
    setError(
      EspBleError::BackendFailure,
      "failed to request connection parameter update");
    return false;
  }
  clearError();
  return true;
}

bool EspBleBluedroid::discoverCharacteristic(
  EspBleConnectionId connectionId,
  const char *serviceUuid,
  const char *characteristicUuid,
  uint32_t timeoutMilliseconds)
{
  return startGattOperation(
    EspBleGattOperation::Discover, connectionId, serviceUuid,
    characteristicUuid, nullptr, 0, true, nullptr, timeoutMilliseconds);
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

bool EspBleBluedroid::readDescriptor(
  EspBleConnectionId connectionId,
  uint16_t descriptorHandle,
  uint32_t timeoutMilliseconds)
{
  if (descriptorHandle == 0)
  {
    setError(EspBleError::InvalidArgument,
      "descriptor handle must be non-zero");
    return false;
  }
  return startGattOperation(
    EspBleGattOperation::ReadDescriptor, connectionId, nullptr, nullptr,
    nullptr, 0, true, nullptr, timeoutMilliseconds, 0, descriptorHandle);
}

bool EspBleBluedroid::writeDescriptor(
  EspBleConnectionId connectionId,
  uint16_t descriptorHandle,
  const uint8_t *data,
  size_t length,
  bool response,
  uint32_t timeoutMilliseconds)
{
  if (descriptorHandle == 0)
  {
    setError(EspBleError::InvalidArgument,
      "descriptor handle must be non-zero");
    return false;
  }
  return startGattOperation(
    EspBleGattOperation::WriteDescriptor, connectionId, nullptr, nullptr,
    data, length, response, nullptr, timeoutMilliseconds, 0,
    descriptorHandle);
}

bool EspBleBluedroid::writeDescriptor(
  EspBleConnectionId connectionId,
  uint16_t descriptorHandle,
  const String &value,
  bool response,
  uint32_t timeoutMilliseconds)
{
  return writeDescriptor(
    connectionId, descriptorHandle,
    reinterpret_cast<const uint8_t *>(value.c_str()), value.length(),
    response, timeoutMilliseconds);
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
  uint16_t characteristicHandle,
  uint16_t descriptorHandle)
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
  const bool handleBased =
    characteristicHandle != 0 || descriptorHandle != 0;
  if ((!databaseDiscovery && !handleBased &&
       (serviceUuid == nullptr || serviceUuid[0] == '\0' ||
        characteristicUuid == nullptr || characteristicUuid[0] == '\0')) ||
      (descriptorOperation && descriptorHandle == 0 &&
       (descriptorUuid == nullptr || descriptorUuid[0] == '\0')) ||
      (data == nullptr && length != 0) || timeoutMilliseconds == 0 ||
      (operation != EspBleGattOperation::Discover &&
       operation != EspBleGattOperation::Read &&
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
    connectionImpl_->gattDescriptorHandle = descriptorHandle;
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

  if (databaseDiscovery)
  {
    EspBleConnectionImpl::GattDatabaseSnapshot *database =
      new (std::nothrow) EspBleConnectionImpl::GattDatabaseSnapshot();
    BLEClient *client = nullptr;
    if (database != nullptr)
    {
      database->connectionId = connectionId;
      std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
      client = connectionImpl_->client;
      if (client != nullptr)
      {
        connectionImpl_->gattDiscoveryDatabase = database;
        connectionImpl_->gattDirectGattcIf = client->getGattcIf();
        connectionImpl_->gattDirectConnectionHandle = client->getConnId();
        connectionImpl_->gattDiscoveryError = EspBleError::None;
        connectionImpl_->gattDiscoveryDetail = "";
        connectionImpl_->gattDirectDiscovery = true;
      }
    }
    if (database == nullptr || client == nullptr)
    {
      delete database;
      std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
      connectionImpl_->gattOperating = false;
      setError(
        database == nullptr
          ? EspBleError::ResourceExhausted
          : EspBleError::InvalidState,
        database == nullptr
          ? "failed to allocate GATT database snapshot"
          : "connection is not an active Central connection");
      return false;
    }
    if (esp_ble_gattc_search_service(
          connectionImpl_->gattDirectGattcIf,
          connectionImpl_->gattDirectConnectionHandle,
          nullptr) != ESP_OK)
    {
      std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
      delete connectionImpl_->gattDiscoveryDatabase;
      connectionImpl_->gattDiscoveryDatabase = nullptr;
      connectionImpl_->gattDirectDiscovery = false;
      connectionImpl_->gattOperating = false;
      setError(
        EspBleError::BackendFailure,
        "failed to request GATT service discovery");
      return false;
    }
    clearError();
    return true;
  }

  if (operation == EspBleGattOperation::Read)
  {
    bool databaseAvailable = false;
    bool characteristicFound = false;
    BLEClient *client = nullptr;
    EspBleGattResult directResult;
    {
      std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
      EspBleConnectionImpl::GattDatabaseSnapshot *database =
        connectionImpl_->gattDatabase;
      databaseAvailable =
        database != nullptr && database->connectionId == connectionId;
      directResult.operation = operation;
      directResult.connectionId = connectionId;
      directResult.serviceUuid =
        serviceUuid == nullptr ? "" : serviceUuid;
      directResult.characteristicUuid =
        characteristicUuid == nullptr ? "" : characteristicUuid;
      directResult.handle = characteristicHandle;
      directResult.response = response;
      if (databaseAvailable)
      {
        for (size_t index = 0;
             index < database->characteristicCount;
             ++index)
        {
          const EspBleGattCharacteristicInfo &candidate =
            database->characteristics[index];
          const bool matches = characteristicHandle != 0
            ? candidate.handle == characteristicHandle
            : uuidEquals(candidate.serviceUuid, serviceUuid) &&
              uuidEquals(
                candidate.characteristicUuid, characteristicUuid);
          if (!matches) continue;
          directResult.serviceUuid = candidate.serviceUuid;
          directResult.characteristicUuid =
            candidate.characteristicUuid;
          directResult.handle = candidate.handle;
          directResult.readable = candidate.readable;
          directResult.writable = candidate.writable;
          directResult.writableWithoutResponse =
            candidate.writableWithoutResponse;
          directResult.notifiable = candidate.notifiable;
          directResult.indicatable = candidate.indicatable;
          characteristicFound = true;
          break;
        }

        if (!characteristicFound || !directResult.readable)
        {
          directResult.error = characteristicFound
            ? EspBleError::InvalidState : EspBleError::NotFound;
          directResult.detail = characteristicFound
            ? "GATT characteristic is not readable"
            : (characteristicHandle != 0
                ? "GATT characteristic handle was not found "
                  "(discover services first)"
                : "GATT characteristic was not found");
          EspBleConnectionImpl::Event event;
          event.type = EspBleConnectionImpl::EventType::GattResult;
          event.gattResult = directResult;
          connectionImpl_->pushEventLocked(event);
          connectionImpl_->gattOperating = false;
        }
        else
        {
          client = connectionImpl_->client;
          if (client == nullptr)
          {
            characteristicFound = false;
            directResult.error = EspBleError::InvalidState;
            directResult.detail =
              "connection is not an active Central connection";
            EspBleConnectionImpl::Event event;
            event.type = EspBleConnectionImpl::EventType::GattResult;
            event.gattResult = directResult;
            connectionImpl_->pushEventLocked(event);
            connectionImpl_->gattOperating = false;
          }
          else
          {
            connectionImpl_->gattDirectGattcIf = client->getGattcIf();
            connectionImpl_->gattDirectConnectionHandle =
              client->getConnId();
            connectionImpl_->gattDirectResult = directResult;
            connectionImpl_->gattDirectCharacteristicRead = true;
          }
        }
      }
    }

    if (databaseAvailable && (!characteristicFound || !directResult.readable))
    {
      clearError();
      return true;
    }
    if (databaseAvailable)
    {
      if (client == nullptr ||
          esp_ble_gattc_read_char(
            connectionImpl_->gattDirectGattcIf,
            connectionImpl_->gattDirectConnectionHandle,
            directResult.handle,
            ESP_GATT_AUTH_REQ_NONE) != ESP_OK)
      {
        std::lock_guard<std::mutex> lock(connectionImpl_->mutex);
        connectionImpl_->gattDirectCharacteristicRead = false;
        connectionImpl_->gattOperating = false;
        setError(
          EspBleError::BackendFailure,
          "failed to request GATT characteristic read");
        return false;
      }
      clearError();
      return true;
    }
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

bool EspBleBluedroid::addToAcceptList(
  const char *address, EspBleAddressType addressType)
{
  if (!initialized_)
  {
    setError(EspBleError::InvalidState, "BLE stack is not initialized");
    return false;
  }
  if (!isValidBleAddress(address) ||
      static_cast<uint8_t>(addressType) >
        static_cast<uint8_t>(EspBleAddressType::RandomIdentity))
  {
    setError(
      EspBleError::InvalidArgument,
      "a valid accept list address and address type are required");
    return false;
  }
  for (size_t index = 0; index < acceptListCount_; ++index)
  {
    if (acceptList_[index].peerAddressType == addressType &&
        acceptList_[index].peerAddress.equalsIgnoreCase(address))
    {
      clearError();
      return true;
    }
  }
  if (acceptListCount_ == MaxAcceptListEntries)
  {
    setError(EspBleError::ResourceExhausted, "accept list is full");
    return false;
  }
  acceptList_[acceptListCount_].peerAddress = address;
  acceptList_[acceptListCount_].peerAddressType = addressType;
  ++acceptListCount_;
  clearError();
  return true;
}

bool EspBleBluedroid::removeFromAcceptList(
  const char *address, EspBleAddressType addressType)
{
  if (!initialized_)
  {
    setError(EspBleError::InvalidState, "BLE stack is not initialized");
    return false;
  }
  if (!isValidBleAddress(address) ||
      static_cast<uint8_t>(addressType) >
        static_cast<uint8_t>(EspBleAddressType::RandomIdentity))
  {
    setError(
      EspBleError::InvalidArgument,
      "a valid accept list address and address type are required");
    return false;
  }
  for (size_t index = 0; index < acceptListCount_; ++index)
  {
    if (acceptList_[index].peerAddressType != addressType ||
        !acceptList_[index].peerAddress.equalsIgnoreCase(address))
      continue;
    for (size_t next = index + 1; next < acceptListCount_; ++next)
    {
      acceptList_[next - 1] = acceptList_[next];
    }
    acceptList_[--acceptListCount_] = EspBleBond();
    clearError();
    return true;
  }
  setError(EspBleError::NotFound, "accept list entry was not found");
  return false;
}

void EspBleBluedroid::clearAcceptList()
{
  for (size_t index = 0; index < acceptListCount_; ++index)
  {
    acceptList_[index] = EspBleBond();
  }
  acceptListCount_ = 0;
}

size_t EspBleBluedroid::acceptListCount() const
{
  return acceptListCount_;
}

bool EspBleBluedroid::acceptListEntry(
  size_t index, EspBleBond &entry) const
{
  if (index >= acceptListCount_) return false;
  entry = acceptList_[index];
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
    bond.peerAddress = String(
      espblebluedroid::internal::formatBleAddress(
        bonds[index].bd_addr).c_str());
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
    if (String(
          espblebluedroid::internal::formatBleAddress(
            bonds[index].bd_addr).c_str()).equalsIgnoreCase(
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

void EspBleBluedroid::onMtuChanged(MtuChangedCallback callback)
{
  mtuChangedCallback_ = std::move(callback);
}

void EspBleBluedroid::onConnectionParametersUpdated(
  ConnectionCallback callback)
{
  connectionParametersUpdatedCallback_ = std::move(callback);
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

void EspBleBluedroid::onCharacteristicDiscovered(GattResultCallback callback)
{
  characteristicDiscoveredCallback_ = std::move(callback);
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

void EspBleBluedroid::onNotification(NotificationCallback callback)
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
  event.gattResult.descriptorHandle = connectionImpl_->gattDescriptorHandle;
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
    else if (event.type == EspBleConnectionImpl::EventType::MtuChanged &&
             mtuChangedCallback_)
    {
      mtuChangedCallback_(event.mtuChanged);
    }
    else if (
      event.type ==
        EspBleConnectionImpl::EventType::ConnectionParametersUpdated &&
      connectionParametersUpdatedCallback_)
    {
      connectionParametersUpdatedCallback_(event.connection);
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
      event.gattResult.operation == EspBleGattOperation::Discover &&
      characteristicDiscoveredCallback_)
    {
      characteristicDiscoveredCallback_(event.gattResult);
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
