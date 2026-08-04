#include "EspBleBluedroid.h"

#include "internal/EspBleBluedroidCodec.h"

#include <BLECharacteristic.h>
#include <BLEDescriptor.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEService.h>
#include <mutex>
#include <new>

namespace
{
uint32_t characteristicProperties(const EspBleGattCharacteristicConfig &config)
{
  uint32_t properties = 0;
  if (config.readable) properties |= BLECharacteristic::PROPERTY_READ;
  if (config.writable) properties |= BLECharacteristic::PROPERTY_WRITE;
  if (config.writableWithoutResponse)
    properties |= BLECharacteristic::PROPERTY_WRITE_NR;
  if (config.notifiable) properties |= BLECharacteristic::PROPERTY_NOTIFY;
  if (config.indicatable) properties |= BLECharacteristic::PROPERTY_INDICATE;
  return properties;
}

// A registration UUID has to be parseable before it reaches the Arduino wrapper.
// BLEUUID stores an unset value for a malformed string and BLEService::
// executeCreate() then copies from its null native pointer, which crashes during
// begin() instead of failing the registration call that caused it.
bool validUuid(const char *uuid)
{
  espblebluedroid::internal::BleUuid parsed;
  return uuid != nullptr && uuid[0] != '\0' &&
    espblebluedroid::internal::parseBleUuid(uuid, parsed);
}

uint16_t accessPermissions(
  bool readable, bool writable, bool encryptedRead, bool encryptedWrite,
  bool authenticatedRead, bool authenticatedWrite)
{
  uint16_t permissions = 0;
  if (authenticatedRead) permissions |= ESP_GATT_PERM_READ_ENC_MITM;
  else if (encryptedRead) permissions |= ESP_GATT_PERM_READ_ENCRYPTED;
  else if (readable) permissions |= ESP_GATT_PERM_READ;
  if (authenticatedWrite) permissions |= ESP_GATT_PERM_WRITE_ENC_MITM;
  else if (encryptedWrite) permissions |= ESP_GATT_PERM_WRITE_ENCRYPTED;
  else if (writable) permissions |= ESP_GATT_PERM_WRITE;
  return permissions;
}
}

struct EspBleGattServerImpl
{
  // The connection lifecycle of a peer that connected to this Server. Bluedroid
  // reports it here and nowhere else, so without these callbacks a peripheral
  // link existed on the air but not in the public API: no onConnected(), no MTU
  // event, no connection() snapshot, and pairing on such a link was dropped.
  // Everything is forwarded to the owner, which holds the connection state, the
  // event queue and the security callbacks for both roles.
  struct ServerCallbacks : BLEServerCallbacks
  {
    explicit ServerCallbacks(EspBleGattServerImpl *value) : impl(value) {}

    void onConnect(BLEServer *, esp_ble_gatts_cb_param_t *parameter) override
    {
      if (parameter == nullptr) return;
      // Bluedroid reports every link to the Server, including one this device
      // opened as a central while a Server happens to be registered. Only the
      // links where this device is the slave belong to the peripheral role.
      if (parameter->connect.link_role != 1) return;
      impl->forwardConnected(parameter->connect.conn_id,
        parameter->connect.remote_bda,
        static_cast<uint8_t>(parameter->connect.ble_addr_type),
        parameter->connect.conn_params.interval,
        parameter->connect.conn_params.latency,
        parameter->connect.conn_params.timeout);
    }
    void onDisconnect(BLEServer *, esp_ble_gatts_cb_param_t *parameter) override
    {
      if (parameter == nullptr) return;
      impl->forwardDisconnected(parameter->disconnect.conn_id,
        static_cast<int>(parameter->disconnect.reason));
    }
    void onMtuChanged(BLEServer *, esp_ble_gatts_cb_param_t *parameter) override
    {
      if (parameter == nullptr) return;
      impl->forwardMtuChanged(
        parameter->mtu.conn_id, parameter->mtu.mtu);
    }
    void onConnParamsUpdate(
      esp_bd_addr_t address,
      uint16_t interval,
      uint16_t latency,
      uint16_t timeout,
      esp_bt_status_t status) override
    {
      if (status != ESP_BT_STATUS_SUCCESS) return;
      impl->forwardParametersUpdated(address, interval, latency, timeout);
    }

    EspBleGattServerImpl *impl;
  };

  struct ServiceDefinition
  {
    String uuid;
    BLEService *backend = nullptr;
  };

  struct CharacteristicDefinition;
  struct DescriptorDefinition;

  struct CharacteristicCallbacks : BLECharacteristicCallbacks
  {
    EspBleGattServerImpl *owner = nullptr;
    size_t index = 0;

    void onRead(
      BLECharacteristic *, esp_ble_gatts_cb_param_t *parameter) override
    {
      if (owner != nullptr) owner->characteristicRead(
        index, parameter == nullptr ? 0 : parameter->read.conn_id);
    }

    void onWrite(
      BLECharacteristic *, esp_ble_gatts_cb_param_t *parameter) override
    {
      if (owner != nullptr) owner->characteristicWritten(
        index, parameter == nullptr ? 0 : parameter->write.conn_id);
    }

    void onStatus(
      BLECharacteristic *, Status status, uint32_t) override
    {
      if (owner != nullptr) owner->sendCompleted(index, status);
    }
  };

  struct DescriptorCallbacks : BLEDescriptorCallbacks
  {
    EspBleGattServerImpl *owner = nullptr;
    size_t index = 0;
    size_t characteristicIndex = 0;
    bool subscription = false;

    void onWrite(BLEDescriptor *descriptor) override
    {
      if (owner == nullptr) return;
      if (subscription)
        owner->subscriptionWritten(characteristicIndex, descriptor);
      else
        owner->descriptorWritten(index, descriptor);
    }
  };

  struct CharacteristicDefinition
  {
    uint16_t serviceId = 0;
    String uuid;
    EspBleGattCharacteristicConfig config;
    String value;
    BLECharacteristic *backend = nullptr;
    BLEDescriptor *cccd = nullptr;
    CharacteristicCallbacks callbacks;
    DescriptorCallbacks cccdCallbacks;
  };

  struct DescriptorDefinition
  {
    uint16_t characteristicId = 0;
    String uuid;
    EspBleGattDescriptorConfig config;
    String value;
    BLEDescriptor *backend = nullptr;
    DescriptorCallbacks callbacks;
  };

  enum class EventType : uint8_t
  {
    Write,
    DescriptorWrite,
    Subscription,
    Sent,
  };

  struct Event
  {
    EventType type = EventType::Write;
    EspBleGattWrite write;
    EspBleGattDescriptorWrite descriptorWrite;
    EspBleGattSubscription subscription;
    EspBleGattSendResult sent;
  };

  static constexpr size_t EventCapacity = 16;
  EspBleGattServer *api;
  mutable std::mutex mutex;
  ServiceDefinition services[EspBleGattServer::MaxServices];
  CharacteristicDefinition characteristics[EspBleGattServer::MaxCharacteristics];
  DescriptorDefinition descriptors[EspBleGattServer::MaxDescriptors];
  size_t serviceCount = 0;
  size_t characteristicCount = 0;
  size_t descriptorCount = 0;
  BLEServer *server = nullptr;
  ServerCallbacks serverCallbacks{this};
  bool realized = false;
  Event events[EventCapacity];
  size_t eventHead = 0;
  size_t eventCount = 0;
  EspBleGattSendResult pendingSend;

  explicit EspBleGattServerImpl(EspBleGattServer *value) : api(value) {}

  // The owner's runtime ID for the peripheral link, so a Server event and
  // connection() name the same connection. The backend handle is only a
  // fallback for the window before the connect callback has been forwarded.
  EspBleConnectionId connectionId() const
  {
    if (server == nullptr || server->getConnectedCount() == 0) return 0;
    const EspBleConnectionId id = api->owner_->peripheralConnectionId();
    return id != 0
      ? id : static_cast<EspBleConnectionId>(server->getConnId()) + 1;
  }

  void forwardConnected(
    uint16_t handle,
    const uint8_t *address,
    uint8_t addressType,
    uint16_t interval,
    uint16_t latency,
    uint16_t timeout)
  {
    api->owner_->peripheralConnected(
      handle, address, addressType, interval, latency, timeout);
  }
  void forwardDisconnected(uint16_t handle, int reason)
  {
    api->owner_->peripheralDisconnected(handle, reason);
  }
  void forwardMtuChanged(uint16_t handle, uint16_t mtu)
  {
    api->owner_->peripheralMtuChanged(handle, mtu);
  }
  void forwardParametersUpdated(
    const uint8_t *address, uint16_t interval, uint16_t latency,
    uint16_t timeout)
  {
    api->owner_->peripheralParametersUpdated(
      address, interval, latency, timeout);
  }

  void push(Event &&event)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (eventCount == EventCapacity)
    {
      eventHead = (eventHead + 1) % EventCapacity;
      --eventCount;
    }
    events[(eventHead + eventCount) % EventCapacity] = std::move(event);
    ++eventCount;
  }

  void characteristicRead(size_t index, uint16_t connectionHandle)
  {
    if (index >= characteristicCount || !api->readCallback_) return;
    CharacteristicDefinition &definition = characteristics[index];
    EspBleGattReadRequest request;
    request.connectionId = static_cast<EspBleConnectionId>(connectionHandle) + 1;
    request.characteristic.id = index + 1;
    request.serviceUuid = services[definition.serviceId - 1].uuid;
    request.characteristicUuid = definition.uuid;
    api->readCallback_(request);
  }

  void characteristicWritten(size_t index, uint16_t connectionHandle)
  {
    if (index >= characteristicCount) return;
    CharacteristicDefinition &definition = characteristics[index];
    definition.value = definition.backend->getValue();
    Event event;
    event.type = EventType::Write;
    event.write.connectionId =
      static_cast<EspBleConnectionId>(connectionHandle) + 1;
    event.write.characteristic.id = index + 1;
    event.write.serviceUuid = services[definition.serviceId - 1].uuid;
    event.write.characteristicUuid = definition.uuid;
    event.write.value = definition.value;
    push(std::move(event));
  }

  void descriptorWritten(size_t index, BLEDescriptor *descriptor)
  {
    if (index >= descriptorCount || descriptor == nullptr) return;
    DescriptorDefinition &definition = descriptors[index];
    definition.value = String(
      reinterpret_cast<const char *>(descriptor->getValue()),
      descriptor->getLength());
    CharacteristicDefinition &characteristic =
      characteristics[definition.characteristicId - 1];
    Event event;
    event.type = EventType::DescriptorWrite;
    event.descriptorWrite.connectionId = connectionId();
    event.descriptorWrite.descriptor.id = index + 1;
    event.descriptorWrite.serviceUuid =
      services[characteristic.serviceId - 1].uuid;
    event.descriptorWrite.characteristicUuid = characteristic.uuid;
    event.descriptorWrite.descriptorUuid = definition.uuid;
    event.descriptorWrite.value = definition.value;
    push(std::move(event));
  }

  void subscriptionWritten(size_t index, BLEDescriptor *descriptor)
  {
    if (index >= characteristicCount || descriptor == nullptr) return;
    CharacteristicDefinition &definition = characteristics[index];
    const uint8_t *value = descriptor->getValue();
    const uint16_t flags = descriptor->getLength() >= 2
      ? static_cast<uint16_t>(value[0] | (value[1] << 8)) : 0;
    Event event;
    event.type = EventType::Subscription;
    event.subscription.connectionId = connectionId();
    event.subscription.characteristic.id = index + 1;
    event.subscription.serviceUuid = services[definition.serviceId - 1].uuid;
    event.subscription.characteristicUuid = definition.uuid;
    event.subscription.notifications = (flags & 0x0001) != 0;
    event.subscription.indications = (flags & 0x0002) != 0;
    push(std::move(event));
  }

  void sendCompleted(size_t index, BLECharacteristicCallbacks::Status status)
  {
    Event event;
    event.type = EventType::Sent;
    {
      std::lock_guard<std::mutex> lock(mutex);
      event.sent = pendingSend;
    }
    if (!event.sent.characteristic.valid())
    {
      event.sent.characteristic.id = index + 1;
      CharacteristicDefinition &definition = characteristics[index];
      event.sent.serviceUuid = services[definition.serviceId - 1].uuid;
      event.sent.characteristicUuid = definition.uuid;
    }
    event.sent.success =
      status == BLECharacteristicCallbacks::SUCCESS_NOTIFY ||
      status == BLECharacteristicCallbacks::SUCCESS_INDICATE;
    if (!event.sent.success)
    {
      event.sent.error = EspBleError::BackendFailure;
      event.sent.detail = "GATT Server send failed";
    }
    push(std::move(event));
  }
};

EspBleGattServer::EspBleGattServer(EspBleBluedroid *owner) : owner_(owner)
{
  impl_ = new (std::nothrow) EspBleGattServerImpl(this);
}

EspBleGattServer::~EspBleGattServer()
{
  delete impl_;
}

EspBleGattService EspBleGattServer::addService(const char *uuid)
{
  EspBleGattService result;
  if (impl_ == nullptr || !validUuid(uuid))
  {
    owner_->setError(EspBleError::InvalidArgument, "invalid GATT Service UUID");
    return result;
  }
  if (owner_->initialized() || impl_->serviceCount == MaxServices)
  {
    owner_->setError(owner_->initialized() ? EspBleError::InvalidState
      : EspBleError::ResourceExhausted,
      owner_->initialized() ? "register GATT Services before begin()"
      : "too many GATT Services");
    return result;
  }
  result.id = ++impl_->serviceCount;
  impl_->services[result.id - 1].uuid = uuid;
  owner_->clearError();
  return result;
}

EspBleGattCharacteristic EspBleGattServer::addCharacteristic(
  EspBleGattService service, const char *uuid,
  const EspBleGattCharacteristicConfig &config)
{
  EspBleGattCharacteristic result;
  if (impl_ == nullptr || !service.valid() || service.id > impl_->serviceCount ||
      !validUuid(uuid))
  {
    owner_->setError(EspBleError::InvalidArgument,
      "invalid GATT Characteristic registration");
    return result;
  }
  if (owner_->initialized() || impl_->characteristicCount == MaxCharacteristics)
  {
    owner_->setError(owner_->initialized() ? EspBleError::InvalidState
      : EspBleError::ResourceExhausted,
      owner_->initialized() ? "register GATT Characteristics before begin()"
      : "too many GATT Characteristics");
    return result;
  }
  for (size_t index = 0; index < impl_->characteristicCount; ++index)
  {
    const auto &existing = impl_->characteristics[index];
    if (existing.serviceId == service.id &&
        BLEUUID(existing.uuid.c_str()).equals(BLEUUID(uuid)))
    {
      owner_->setError(EspBleError::InvalidArgument,
        "this library cannot address duplicate Characteristic UUIDs in one Service");
      return result;
    }
  }
  result.id = ++impl_->characteristicCount;
  auto &definition = impl_->characteristics[result.id - 1];
  definition.serviceId = service.id;
  definition.uuid = uuid;
  definition.config = config;
  owner_->clearError();
  return result;
}

EspBleGattDescriptor EspBleGattServer::addDescriptor(
  EspBleGattCharacteristic characteristic, const char *uuid,
  const EspBleGattDescriptorConfig &config)
{
  EspBleGattDescriptor result;
  if (impl_ == nullptr || !characteristic.valid() ||
      characteristic.id > impl_->characteristicCount || !validUuid(uuid) ||
      config.maximumLength == 0)
  {
    owner_->setError(EspBleError::InvalidArgument,
      "invalid GATT Descriptor registration");
    return result;
  }
  if (owner_->initialized() || impl_->descriptorCount == MaxDescriptors)
  {
    owner_->setError(owner_->initialized() ? EspBleError::InvalidState
      : EspBleError::ResourceExhausted,
      owner_->initialized() ? "register GATT Descriptors before begin()"
      : "too many GATT Descriptors");
    return result;
  }
  for (size_t index = 0; index < impl_->descriptorCount; ++index)
  {
    const auto &existing = impl_->descriptors[index];
    if (existing.characteristicId == characteristic.id &&
        BLEUUID(existing.uuid.c_str()).equals(BLEUUID(uuid)))
    {
      owner_->setError(EspBleError::InvalidArgument,
        "duplicate Descriptor UUID in one Characteristic");
      return result;
    }
  }
  result.id = ++impl_->descriptorCount;
  auto &definition = impl_->descriptors[result.id - 1];
  definition.characteristicId = characteristic.id;
  definition.uuid = uuid;
  definition.config = config;
  owner_->clearError();
  return result;
}

bool EspBleGattServer::setValue(
  EspBleGattCharacteristic characteristic, const uint8_t *data, size_t length)
{
  if (impl_ == nullptr || !characteristic.valid() ||
      characteristic.id > impl_->characteristicCount ||
      (data == nullptr && length != 0))
  {
    owner_->setError(EspBleError::InvalidArgument, "invalid GATT value");
    return false;
  }
  auto &definition = impl_->characteristics[characteristic.id - 1];
  definition.value = length == 0 ? String() : String(
    reinterpret_cast<const char *>(data), length);
  if (definition.backend != nullptr)
    definition.backend->setValue(data, length);
  owner_->clearError();
  return true;
}

bool EspBleGattServer::setValue(
  EspBleGattCharacteristic characteristic, const String &value)
{
  return setValue(characteristic,
    reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
}

bool EspBleGattServer::value(
  EspBleGattCharacteristic characteristic, String &value) const
{
  if (impl_ == nullptr || !characteristic.valid() ||
      characteristic.id > impl_->characteristicCount) return false;
  value = impl_->characteristics[characteristic.id - 1].value;
  return true;
}

bool EspBleGattServer::setDescriptorValue(
  EspBleGattDescriptor descriptor, const uint8_t *data, size_t length)
{
  if (impl_ == nullptr || !descriptor.valid() ||
      descriptor.id > impl_->descriptorCount ||
      (data == nullptr && length != 0) ||
      length > impl_->descriptors[descriptor.id - 1].config.maximumLength)
  {
    owner_->setError(EspBleError::InvalidArgument,
      "invalid GATT Descriptor value");
    return false;
  }
  auto &definition = impl_->descriptors[descriptor.id - 1];
  definition.value = length == 0 ? String() : String(
    reinterpret_cast<const char *>(data), length);
  if (definition.backend != nullptr) definition.backend->setValue(data, length);
  owner_->clearError();
  return true;
}

bool EspBleGattServer::setDescriptorValue(
  EspBleGattDescriptor descriptor, const String &value)
{
  return setDescriptorValue(descriptor,
    reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
}

bool EspBleGattServer::descriptorValue(
  EspBleGattDescriptor descriptor, String &value) const
{
  if (impl_ == nullptr || !descriptor.valid() ||
      descriptor.id > impl_->descriptorCount) return false;
  value = impl_->descriptors[descriptor.id - 1].value;
  return true;
}

bool EspBleGattServer::realize()
{
  if (impl_ == nullptr || impl_->serviceCount == 0) return true;
  impl_->server = BLEDevice::createServer();
  if (impl_->server == nullptr)
  {
    owner_->setError(EspBleError::ResourceExhausted,
      "failed to create GATT Server");
    return false;
  }
  impl_->server->setCallbacks(&impl_->serverCallbacks);
  for (size_t serviceIndex = 0; serviceIndex < impl_->serviceCount; ++serviceIndex)
  {
    auto &service = impl_->services[serviceIndex];
    service.backend = impl_->server->createService(
      BLEUUID(service.uuid.c_str()), 15,
      static_cast<uint8_t>(serviceIndex));
    if (service.backend == nullptr) return false;
    for (size_t characteristicIndex = 0;
         characteristicIndex < impl_->characteristicCount; ++characteristicIndex)
    {
      auto &characteristic = impl_->characteristics[characteristicIndex];
      if (characteristic.serviceId != serviceIndex + 1) continue;
      characteristic.backend = service.backend->createCharacteristic(
        characteristic.uuid.c_str(), characteristicProperties(characteristic.config));
      if (characteristic.backend == nullptr) return false;
      characteristic.backend->setAccessPermissions(accessPermissions(
        characteristic.config.readable, characteristic.config.writable ||
          characteristic.config.writableWithoutResponse,
        characteristic.config.encryptedRead, characteristic.config.encryptedWrite,
        characteristic.config.authenticatedRead,
        characteristic.config.authenticatedWrite));
      characteristic.callbacks.owner = impl_;
      characteristic.callbacks.index = characteristicIndex;
      characteristic.backend->setCallbacks(&characteristic.callbacks);
      characteristic.backend->setValue(
        reinterpret_cast<const uint8_t *>(characteristic.value.c_str()),
        characteristic.value.length());
      if (characteristic.config.notifiable || characteristic.config.indicatable)
      {
        characteristic.cccd = new BLEDescriptor(BLEUUID((uint16_t)0x2902), 2);
        const uint8_t disabled[] = {0, 0};
        characteristic.cccd->setValue(disabled, sizeof(disabled));
        characteristic.cccd->setAccessPermissions(
          ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);
        characteristic.cccdCallbacks.owner = impl_;
        characteristic.cccdCallbacks.characteristicIndex = characteristicIndex;
        characteristic.cccdCallbacks.subscription = true;
        characteristic.cccd->setCallbacks(&characteristic.cccdCallbacks);
        characteristic.backend->addDescriptor(characteristic.cccd);
      }
      for (size_t descriptorIndex = 0;
           descriptorIndex < impl_->descriptorCount; ++descriptorIndex)
      {
        auto &descriptor = impl_->descriptors[descriptorIndex];
        if (descriptor.characteristicId != characteristicIndex + 1) continue;
        descriptor.backend = new BLEDescriptor(
          descriptor.uuid.c_str(), descriptor.config.maximumLength);
        descriptor.backend->setAccessPermissions(accessPermissions(
          descriptor.config.readable, descriptor.config.writable,
          descriptor.config.encryptedRead, descriptor.config.encryptedWrite,
          descriptor.config.authenticatedRead,
          descriptor.config.authenticatedWrite));
        descriptor.callbacks.owner = impl_;
        descriptor.callbacks.index = descriptorIndex;
        descriptor.backend->setCallbacks(&descriptor.callbacks);
        descriptor.backend->setValue(
          reinterpret_cast<const uint8_t *>(descriptor.value.c_str()),
          descriptor.value.length());
        characteristic.backend->addDescriptor(descriptor.backend);
      }
    }
    service.backend->start();
  }
  impl_->realized = true;
  return true;
}

void EspBleGattServer::resetBackend()
{
  if (impl_ == nullptr) return;
  impl_->server = nullptr;
  impl_->realized = false;
  impl_->eventHead = 0;
  impl_->eventCount = 0;
  for (size_t index = 0; index < impl_->serviceCount; ++index)
    impl_->services[index].backend = nullptr;
  for (size_t index = 0; index < impl_->characteristicCount; ++index)
  {
    impl_->characteristics[index].backend = nullptr;
    impl_->characteristics[index].cccd = nullptr;
  }
  for (size_t index = 0; index < impl_->descriptorCount; ++index)
    impl_->descriptors[index].backend = nullptr;
}

bool EspBleGattServer::send(
  EspBleConnectionId connectionId, EspBleGattCharacteristic characteristic,
  const uint8_t *data, size_t length, bool indication)
{
  if (impl_ == nullptr || !owner_->initialized() || !impl_->realized ||
      !characteristic.valid() || characteristic.id > impl_->characteristicCount ||
      (data == nullptr && length != 0))
  {
    owner_->setError(EspBleError::InvalidArgument, "invalid GATT Server send");
    return false;
  }
  auto &definition = impl_->characteristics[characteristic.id - 1];
  if ((indication && !definition.config.indicatable) ||
      (!indication && !definition.config.notifiable))
  {
    owner_->setError(EspBleError::InvalidState,
      indication ? "Characteristic is not indicatable"
                 : "Characteristic is not notifiable");
    return false;
  }
  if (connectionId != 0 && connectionId != impl_->connectionId())
  {
    owner_->setError(EspBleError::NotFound,
      "Peripheral connection ID was not found");
    return false;
  }
  setValue(characteristic, data, length);
  impl_->pendingSend.connectionId = connectionId;
  impl_->pendingSend.characteristic = characteristic;
  impl_->pendingSend.serviceUuid =
    impl_->services[definition.serviceId - 1].uuid;
  impl_->pendingSend.characteristicUuid = definition.uuid;
  impl_->pendingSend.value = definition.value;
  impl_->pendingSend.indication = indication;
  if (indication) definition.backend->indicate();
  else definition.backend->notify();
  owner_->clearError();
  return true;
}

bool EspBleGattServer::notify(
  EspBleGattCharacteristic characteristic, const uint8_t *data, size_t length)
{ return send(0, characteristic, data, length, false); }
bool EspBleGattServer::notify(
  EspBleGattCharacteristic characteristic, const String &value)
{ return notify(characteristic,
    reinterpret_cast<const uint8_t *>(value.c_str()), value.length()); }
bool EspBleGattServer::indicate(
  EspBleGattCharacteristic characteristic, const uint8_t *data, size_t length)
{ return send(0, characteristic, data, length, true); }
bool EspBleGattServer::indicate(
  EspBleGattCharacteristic characteristic, const String &value)
{ return indicate(characteristic,
    reinterpret_cast<const uint8_t *>(value.c_str()), value.length()); }
bool EspBleGattServer::notify(
  EspBleConnectionId connectionId, EspBleGattCharacteristic characteristic,
  const uint8_t *data, size_t length)
{ return send(connectionId, characteristic, data, length, false); }
bool EspBleGattServer::notify(
  EspBleConnectionId connectionId, EspBleGattCharacteristic characteristic,
  const String &value)
{ return notify(connectionId, characteristic,
    reinterpret_cast<const uint8_t *>(value.c_str()), value.length()); }
bool EspBleGattServer::indicate(
  EspBleConnectionId connectionId, EspBleGattCharacteristic characteristic,
  const uint8_t *data, size_t length)
{ return send(connectionId, characteristic, data, length, true); }
bool EspBleGattServer::indicate(
  EspBleConnectionId connectionId, EspBleGattCharacteristic characteristic,
  const String &value)
{ return indicate(connectionId, characteristic,
    reinterpret_cast<const uint8_t *>(value.c_str()), value.length()); }

void EspBleGattServer::onWritten(WriteCallback callback)
{ writtenCallback_ = std::move(callback); }
void EspBleGattServer::onRead(ReadCallback callback)
{ readCallback_ = std::move(callback); }
void EspBleGattServer::onDescriptorWritten(DescriptorWriteCallback callback)
{ descriptorWrittenCallback_ = std::move(callback); }
void EspBleGattServer::onSubscriptionChanged(SubscriptionCallback callback)
{ subscriptionCallback_ = std::move(callback); }
void EspBleGattServer::onSent(SendCallback callback)
{ sentCallback_ = std::move(callback); }

void EspBleGattServer::update()
{
  if (impl_ == nullptr) return;
  while (true)
  {
    EspBleGattServerImpl::Event event;
    {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      if (impl_->eventCount == 0) break;
      event = std::move(impl_->events[impl_->eventHead]);
      impl_->eventHead = (impl_->eventHead + 1) % EspBleGattServerImpl::EventCapacity;
      --impl_->eventCount;
    }
    if (event.type == EspBleGattServerImpl::EventType::Write && writtenCallback_)
      writtenCallback_(event.write);
    else if (event.type == EspBleGattServerImpl::EventType::DescriptorWrite &&
             descriptorWrittenCallback_)
      descriptorWrittenCallback_(event.descriptorWrite);
    else if (event.type == EspBleGattServerImpl::EventType::Subscription &&
             subscriptionCallback_)
      subscriptionCallback_(event.subscription);
    else if (event.type == EspBleGattServerImpl::EventType::Sent && sentCallback_)
      sentCallback_(event.sent);
  }
}
