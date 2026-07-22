#ifndef ESP_BLE_BLUEDROID_H
#define ESP_BLE_BLUEDROID_H

#include <Arduino.h>
#include <functional>
#include <sdkconfig.h>

#if !defined(CONFIG_BLUEDROID_ENABLED) || defined(CONFIG_NIMBLE_ENABLED)
#error "EspBleBluedroid requires the Bluedroid backend bundled with Arduino-ESP32"
#endif

#include "espblebluedroid_version.h"

enum class EspBleError : uint8_t
{
  None = 0,
  InvalidState,
  InvalidArgument,
  BackendFailure,
  ResourceExhausted,
  NotFound,
  Timeout,
  Unsupported,
};

enum class EspBleSecurityIoCapability : uint8_t
{
  None = 0,
  DisplayOnly,
  KeyboardOnly,
  DisplayYesNo,
};

struct EspBleSecurityConfig
{
  bool enabled = false;
  bool bonding = true;
  bool pairOnConnect = true;
  bool mitm = false;
  EspBleSecurityIoCapability ioCapability = EspBleSecurityIoCapability::None;
  bool staticPasskeyEnabled = false;
  uint32_t staticPasskey = 0;
};

struct EspBleConfig
{
  const char *deviceName = "EspBleBluedroid";
  uint16_t preferredMtu = 23;
  EspBleSecurityConfig security;
};

struct EspBleScanConfig
{
  bool active = true;
  bool wantDuplicates = false;
  uint16_t intervalMilliseconds = 100;
  uint16_t windowMilliseconds = 50;
  uint32_t durationSeconds = 0;
};

enum class EspBleAddressType : uint8_t
{
  Public = 0,
  Random,
  PublicIdentity,
  RandomIdentity,
};

struct EspBleScanResult
{
  static constexpr size_t MaxServiceUuids = 8;

  String address;
  EspBleAddressType addressType = EspBleAddressType::Public;
  String name;
  int rssi = 0;
  bool connectable = false;
  bool scannable = false;
  String manufacturerData;
  String serviceUuids[MaxServiceUuids];
  size_t serviceUuidCount = 0;

  bool hasName() const;
  bool hasManufacturerData() const;
  bool advertisesService(const char *uuid) const;
};

enum class EspBleRole : uint8_t
{
  Central = 0,
  Peripheral,
};

using EspBleConnectionId = uint32_t;

struct EspBleConnection
{
  EspBleConnectionId id = 0;
  uint16_t handle = 0xffff;
  String peerAddress;
  EspBleAddressType peerAddressType = EspBleAddressType::Public;
  EspBleRole localRole = EspBleRole::Central;
  uint16_t mtu = 23;
  bool encrypted = false;
  bool authenticated = false;
  bool bonded = false;
  uint8_t encryptionKeySize = 0;
  int disconnectReason = 0;

  size_t maximumNotificationPayload() const;
};

struct EspBleConnectionFailure
{
  String peerAddress;
  EspBleError error = EspBleError::BackendFailure;
  String detail;
};

class EspBleBluedroid;
struct EspBleScannerImpl;
struct EspBleConnectionImpl;

class EspBleAdvertising
{
public:
  static constexpr size_t MaxServiceUuids = 4;

  void clear();
  void setName(const char *name);
  bool addServiceUuid(const char *uuid);
  void setManufacturerData(const uint8_t *data, size_t length);
  void setAppearance(uint16_t appearance);
  void setScanResponseEnabled(bool enabled);
  void setConnectable(bool connectable);
  bool setInterval(uint16_t minMilliseconds, uint16_t maxMilliseconds);
  bool start(uint32_t durationSeconds = 0);
  bool stop();
  bool isAdvertising() const;

private:
  friend class EspBleBluedroid;

  explicit EspBleAdvertising(EspBleBluedroid *owner);
  void update();

  EspBleBluedroid *owner_;
  String name_;
  String manufacturerData_;
  String serviceUuids_[MaxServiceUuids];
  size_t serviceUuidCount_ = 0;
  uint16_t appearance_ = 0;
  bool scanResponseEnabled_ = true;
  bool connectable_ = true;
  uint16_t intervalMinMs_ = 0;
  uint16_t intervalMaxMs_ = 0;
  bool advertising_ = false;
  uint32_t startedAtMs_ = 0;
  uint32_t durationMs_ = 0;
};

class EspBleScanner
{
public:
  using ResultCallback = std::function<void(const EspBleScanResult &result)>;

  void onResult(ResultCallback callback);
  bool start(const EspBleScanConfig &config = EspBleScanConfig());
  bool stop();
  bool isScanning() const;
  size_t droppedResultCount() const;

private:
  friend class EspBleBluedroid;
  friend struct EspBleScannerImpl;

  explicit EspBleScanner(EspBleBluedroid *owner);
  ~EspBleScanner();
  void dispatchPendingResults();
  void flushPendingResults();

  EspBleBluedroid *owner_;
  ResultCallback resultCallback_;
  EspBleScannerImpl *impl_ = nullptr;
};

class EspBleBluedroid
{
public:
  using ConnectionCallback =
    std::function<void(const EspBleConnection &connection)>;
  using ConnectionFailureCallback =
    std::function<void(const EspBleConnectionFailure &failure)>;

  EspBleBluedroid();
  ~EspBleBluedroid();

  EspBleBluedroid(const EspBleBluedroid &) = delete;
  EspBleBluedroid &operator=(const EspBleBluedroid &) = delete;

  bool begin(const EspBleConfig &config = EspBleConfig());
  void end();
  void update();

  bool initialized() const;
  EspBleAdvertising &advertising();
  EspBleScanner &scanner();

  bool connect(
    const EspBleScanResult &scanResult,
    uint32_t timeoutMilliseconds = 10000);
  bool connect(
    const char *address,
    EspBleAddressType addressType,
    uint32_t timeoutMilliseconds = 10000);
  bool disconnect(EspBleConnectionId connectionId);
  size_t connectionCount() const;
  bool connection(
    EspBleConnectionId connectionId, EspBleConnection &connection) const;
  size_t droppedEventCount() const;

  void onConnected(ConnectionCallback callback);
  void onDisconnected(ConnectionCallback callback);
  void onConnectionFailed(ConnectionFailureCallback callback);

  EspBleError lastError() const;
  const char *lastErrorName() const;
  const String &lastErrorDetail() const;
  void clearError();

private:
  friend class EspBleAdvertising;
  friend class EspBleScanner;

  void setError(EspBleError error, const char *detail = nullptr);
  void dispatchConnectionEvents();

  EspBleAdvertising advertising_;
  EspBleScanner scanner_;
  EspBleConnectionImpl *connectionImpl_ = nullptr;
  ConnectionCallback connectedCallback_;
  ConnectionCallback disconnectedCallback_;
  ConnectionFailureCallback connectionFailedCallback_;
  bool initialized_ = false;
  String activeDeviceName_;
  uint16_t activePreferredMtu_ = 23;
  EspBleSecurityConfig activeSecurity_;
  EspBleError lastError_ = EspBleError::None;
  String lastErrorDetail_;
};

#endif // ESP_BLE_BLUEDROID_H
