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

// Which address Legacy Advertising presents to BLE peers.
enum class EspBleOwnAddressType : uint8_t
{
  Public = 0,
  RandomStatic,
  ResolvablePrivate,
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

enum class EspBluedroidClassicSecurityIoCapability : uint8_t
{
  None = 0,
  DisplayOnly,
  KeyboardOnly,
  DisplayYesNo,
};

struct EspBluedroidClassicSecurityConfig
{
  bool enabled = false;
  EspBluedroidClassicSecurityIoCapability ioCapability =
    EspBluedroidClassicSecurityIoCapability::None;
  uint32_t responseTimeoutMilliseconds = 30000;
};

struct EspBluedroidClassicBond
{
  String peerAddress;
};

struct EspBleConfig
{
  const char *deviceName = "EspBleBluedroid";
  uint16_t preferredMtu = 247;
  EspBleSecurityConfig security;
  EspBluedroidClassicSecurityConfig classicSecurity;
  // Advertising address privacy. RandomStatic hides the factory public address
  // with one generated identity. ResolvablePrivate enables controller-managed
  // RPA privacy and is useful with bonding so peers resolve address rotation.
  EspBleOwnAddressType ownAddressType = EspBleOwnAddressType::Public;
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

struct EspBleServiceData
{
  String uuid;
  String data;
};

struct EspBleScanResult
{
  static constexpr size_t MaxServiceUuids = 8;
  static constexpr size_t MaxServiceData = 4;

  String address;
  EspBleAddressType addressType = EspBleAddressType::Public;
  String name;
  int rssi = 0;
  bool connectable = false;
  bool scannable = false;
  String manufacturerData;
  EspBleServiceData serviceData[MaxServiceData];
  size_t serviceDataCount = 0;
  String serviceUuids[MaxServiceUuids];
  size_t serviceUuidCount = 0;
  uint16_t appearance = 0;
  int8_t txPowerLevel = 0;
  bool txPowerLevelPresent = false;

  bool hasName() const;
  bool hasManufacturerData() const;
  bool hasServiceData() const;
  bool hasAppearance() const;
  bool hasTxPowerLevel() const;
  bool serviceDataFor(const char *uuid, String &data) const;
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
  // Meaningful in onDisconnected(): the HCI disconnection reason, or 0 when
  // unavailable. It is 0 in connection-state update callbacks.
  int disconnectReason = 0;
  // Current BLE connection parameters. Interval uses 1.25 ms units, timeout
  // uses 10 ms units, and latency counts skipped connection events.
  uint16_t connectionInterval = 0;
  uint16_t peripheralLatency = 0;
  uint16_t supervisionTimeout = 0;

  size_t maximumNotificationPayload() const;
};

struct EspBleMtuChanged
{
  EspBleConnection connection;
  uint16_t previousMtu = 23;
};

struct EspBleConnectionFailure
{
  String peerAddress;
  EspBleError error = EspBleError::BackendFailure;
  String detail;
};

struct EspBleSecurityChanged
{
  EspBleConnection connection;
  bool success = false;
  EspBleError error = EspBleError::None;
  String detail;
};

struct EspBlePasskeyDisplayed
{
  EspBleConnection connection;
  uint32_t passkey = 0;
};

struct EspBleBond
{
  String peerAddress;
  EspBleAddressType peerAddressType = EspBleAddressType::Public;
};

enum class EspBleAdvertisingFilterPolicy : uint8_t
{
  Any = 0,
  ScanRequestFromAcceptList,
  ConnectionFromAcceptList,
  Both,
};

enum class EspBleGattOperation : uint8_t
{
  Discover = 0,
  Read,
  Write,
  Subscribe,
  Unsubscribe,
  DiscoverServices,
  ReadDescriptor,
  WriteDescriptor,
};

struct EspBleGattResult
{
  EspBleGattOperation operation = EspBleGattOperation::Discover;
  EspBleConnectionId connectionId = 0;
  String serviceUuid;
  String characteristicUuid;
  String descriptorUuid;
  uint16_t handle = 0;
  bool success = false;
  EspBleError error = EspBleError::None;
  String detail;
  String value;
  bool readable = false;
  bool writable = false;
  bool writableWithoutResponse = false;
  bool notifiable = false;
  bool indicatable = false;
  bool subscribedToNotifications = false;
  bool subscribedToIndications = false;
  bool response = true;
};

struct EspBleGattNotification
{
  EspBleConnectionId connectionId = 0;
  String serviceUuid;
  String characteristicUuid;
  uint16_t handle = 0;
  String value;
  bool indication = false;
};

struct EspBleGattServiceInfo
{
  String serviceUuid;
  uint16_t handle = 0;
};

struct EspBleGattCharacteristicInfo
{
  String serviceUuid;
  String characteristicUuid;
  uint16_t handle = 0;
  bool readable = false;
  bool writable = false;
  bool writableWithoutResponse = false;
  bool notifiable = false;
  bool indicatable = false;
};

struct EspBleGattDescriptorInfo
{
  String serviceUuid;
  String characteristicUuid;
  String descriptorUuid;
  uint16_t handle = 0;
};

struct EspBluedroidCapabilities
{
  bool ble = true;
  bool classic = false;
  bool dualMode = false;
  bool classicInquiry = false;
  bool classicSpp = false;
};

struct EspBluedroidClassicInquiryConfig
{
  uint32_t durationSeconds = 10;
  uint8_t maxResponses = 0;
};

struct EspBluedroidClassicInquiryResult
{
  String address;
  String name;
  uint32_t classOfDevice = 0;
  int rssi = 0;
  bool hasClassOfDevice = false;
  bool hasRssi = false;
};

struct EspBluedroidClassicInquiryComplete
{
  bool cancelled = false;
};

using EspBluedroidSppSessionId = uint32_t;

enum class EspBluedroidSppSecurity : uint8_t
{
  None = 0,
  Authenticate,
  AuthenticatedEncrypted,
};

struct EspBluedroidSppServerConfig
{
  const char *serviceName = "EspBleBluedroid SPP";
  uint8_t channel = 0;
  EspBluedroidSppSecurity security = EspBluedroidSppSecurity::None;
};

struct EspBluedroidSppSession
{
  EspBluedroidSppSessionId id = 0;
  String peerAddress;
  bool incoming = false;
  bool authenticated = false;
  bool encrypted = false;
};

struct EspBluedroidClassicSecurityChanged
{
  String peerAddress;
  bool success = false;
  int status = 0;
};

struct EspBluedroidClassicNumericComparison
{
  String peerAddress;
  uint32_t value = 0;
};

struct EspBluedroidClassicPasskeyDisplayed
{
  String peerAddress;
  uint32_t passkey = 0;
};

struct EspBluedroidClassicPasskeyRequested
{
  String peerAddress;
};

struct EspBluedroidSppData
{
  EspBluedroidSppSessionId sessionId = 0;
  String value;
};

struct EspBluedroidSppWriteResult
{
  EspBluedroidSppSessionId sessionId = 0;
  size_t length = 0;
  bool success = false;
  EspBleError error = EspBleError::None;
  String detail;
};

struct EspBluedroidSppConnectionFailure
{
  String peerAddress;
  EspBleError error = EspBleError::BackendFailure;
  String detail;
};

class EspBleBluedroid;
struct EspBleScannerImpl;
struct EspBleConnectionImpl;
struct EspBluedroidClassicInquiryImpl;
struct EspBluedroidSppImpl;
struct EspBluedroidClassicImpl;

class EspBleAdvertisingData
{
public:
  static constexpr size_t MaxServiceUuids = 4;
  static constexpr size_t MaxServiceData = 4;

  void clear();
  void setName(const char *name);
  bool addServiceUuid(const char *uuid);
  void setManufacturerData(const uint8_t *data, size_t length);
  bool addServiceData(const char *uuid, const uint8_t *data, size_t length);
  void setAppearance(uint16_t appearance);
  void setTxPowerIncluded(bool included);
  bool isEmpty() const;

private:
  friend class EspBleAdvertising;

  String name_;
  String manufacturerData_;
  EspBleServiceData serviceData_[MaxServiceData];
  size_t serviceDataCount_ = 0;
  String serviceUuids_[MaxServiceUuids];
  size_t serviceUuidCount_ = 0;
  uint16_t appearance_ = 0;
  bool txPowerIncluded_ = false;
};

class EspBleAdvertising
{
public:
  static constexpr size_t MaxServiceUuids =
    EspBleAdvertisingData::MaxServiceUuids;

  void clear();
  EspBleAdvertisingData &data();
  EspBleAdvertisingData &scanResponse();

  void setName(const char *name);
  bool addServiceUuid(const char *uuid);
  void setManufacturerData(const uint8_t *data, size_t length);
  bool addServiceData(
    const char *uuid, const uint8_t *data, size_t length);
  void setAppearance(uint16_t appearance);
  void setScanResponseEnabled(bool enabled);
  void setFilterPolicy(EspBleAdvertisingFilterPolicy policy);
  EspBleAdvertisingFilterPolicy filterPolicy() const;
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
  EspBleAdvertisingData data_;
  EspBleAdvertisingData scanResponseData_;
  bool scanResponseEnabled_ = true;
  EspBleAdvertisingFilterPolicy filterPolicy_ =
    EspBleAdvertisingFilterPolicy::Any;
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
#ifdef ESP_BLE_BLUEDROID_TESTING
  bool injectResultForTest(const EspBleScanResult &result);
  size_t pendingResultCountForTest() const;
#endif

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

class EspBluedroidClassicInquiry
{
public:
  using ResultCallback =
    std::function<void(const EspBluedroidClassicInquiryResult &result)>;
  using CompleteCallback =
    std::function<void(const EspBluedroidClassicInquiryComplete &event)>;

  void onResult(ResultCallback callback);
  void onComplete(CompleteCallback callback);
  bool start(
    const EspBluedroidClassicInquiryConfig &config =
      EspBluedroidClassicInquiryConfig());
  bool stop();
  bool isRunning() const;
  size_t droppedResultCount() const;

private:
  friend class EspBluedroidClassic;
  friend struct EspBluedroidClassicInquiryImpl;

  explicit EspBluedroidClassicInquiry(EspBleBluedroid *owner);
  ~EspBluedroidClassicInquiry();
  bool begin(const char *deviceName);
  void end();
  void update();

  EspBleBluedroid *owner_;
  ResultCallback resultCallback_;
  CompleteCallback completeCallback_;
  EspBluedroidClassicInquiryImpl *impl_ = nullptr;
};

class EspBluedroidSpp
{
public:
  static constexpr size_t WriteQueueCapacity = 8;
  static constexpr size_t ReceiveBufferCapacity = 2048;
  static constexpr size_t MaximumWriteSize = 990;
  using ServerStartedCallback = std::function<void()>;
  using SessionCallback =
    std::function<void(const EspBluedroidSppSession &session)>;
  using DataCallback = std::function<void(const EspBluedroidSppData &event)>;
  using WriteCompletedCallback =
    std::function<void(const EspBluedroidSppWriteResult &result)>;
  using ConnectionFailureCallback =
    std::function<void(const EspBluedroidSppConnectionFailure &failure)>;

  void onServerStarted(ServerStartedCallback callback);
  void onConnected(SessionCallback callback);
  void onDisconnected(SessionCallback callback);
  void onData(DataCallback callback);
  void onWriteCompleted(WriteCompletedCallback callback);
  void onConnectionFailed(ConnectionFailureCallback callback);
  bool connect(
    const char *address,
    uint32_t timeoutMilliseconds = 10000,
    EspBluedroidSppSecurity security = EspBluedroidSppSecurity::None);
  bool startServer(
    const EspBluedroidSppServerConfig &config =
      EspBluedroidSppServerConfig());
  bool stopServer();
  bool serverRunning() const;
  size_t sessionCount() const;
  bool session(
    EspBluedroidSppSessionId sessionId,
    EspBluedroidSppSession &session) const;
  bool write(
    EspBluedroidSppSessionId sessionId,
    const uint8_t *data,
    size_t length);
  bool write(
    EspBluedroidSppSessionId sessionId,
    const String &value);
  bool disconnect(EspBluedroidSppSessionId sessionId);
  size_t pendingWriteCount() const;
  size_t pendingWriteCount(EspBluedroidSppSessionId sessionId) const;
  size_t droppedWriteCount() const;
  size_t available(EspBluedroidSppSessionId sessionId) const;
  int peek(EspBluedroidSppSessionId sessionId) const;
  int read(EspBluedroidSppSessionId sessionId);
  size_t read(
    EspBluedroidSppSessionId sessionId,
    uint8_t *data,
    size_t length);
  size_t droppedReceiveByteCount() const;
  size_t droppedEventCount() const;

private:
  friend class EspBluedroidClassic;
  friend class EspBluedroidSppSerial;
  friend struct EspBluedroidSppImpl;

  explicit EspBluedroidSpp(EspBleBluedroid *owner);
  ~EspBluedroidSpp();
  bool begin();
  void end();
  void update();

  EspBleBluedroid *owner_;
  ServerStartedCallback serverStartedCallback_;
  SessionCallback connectedCallback_;
  SessionCallback disconnectedCallback_;
  DataCallback dataCallback_;
  WriteCompletedCallback writeCompletedCallback_;
  ConnectionFailureCallback connectionFailedCallback_;
  EspBluedroidSppImpl *impl_ = nullptr;
};

class EspBluedroidSppSerial : public Stream
{
public:
  explicit EspBluedroidSppSerial(EspBleBluedroid &bluetooth);

  bool connected() const;
  explicit operator bool() const;
  EspBluedroidSppSessionId sessionId() const;

  int available() override;
  int peek() override;
  int read() override;
  int availableForWrite() override;
  void flush() override;
  size_t write(uint8_t value) override;
  size_t write(const uint8_t *data, size_t length) override;
  using Print::write;

private:
  EspBluedroidSppSessionId resolvedSessionId() const;

  EspBluedroidSpp &spp_;
};

class EspBluedroidClassic
{
public:
  using SecurityChangedCallback =
    std::function<void(const EspBluedroidClassicSecurityChanged &event)>;
  using NumericComparisonCallback =
    std::function<void(const EspBluedroidClassicNumericComparison &event)>;
  using PasskeyDisplayedCallback =
    std::function<void(const EspBluedroidClassicPasskeyDisplayed &event)>;
  using PasskeyRequestedCallback =
    std::function<void(const EspBluedroidClassicPasskeyRequested &event)>;

  EspBluedroidClassicInquiry &inquiry();
  EspBluedroidSpp &spp();
  void onSecurityChanged(SecurityChangedCallback callback);
  void onNumericComparisonRequested(NumericComparisonCallback callback);
  void onPasskeyDisplayed(PasskeyDisplayedCallback callback);
  void onPasskeyRequested(PasskeyRequestedCallback callback);
  bool confirmNumericComparison(const char *peerAddress, bool accept);
  bool providePasskey(const char *peerAddress, uint32_t passkey);
  size_t bondCount() const;
  bool bond(size_t index, EspBluedroidClassicBond &bond) const;
  bool deleteBond(const EspBluedroidClassicBond &bond);
  bool deleteAllBonds();

private:
  friend class EspBleBluedroid;
  friend struct EspBluedroidClassicImpl;

  explicit EspBluedroidClassic(EspBleBluedroid *owner);
  ~EspBluedroidClassic();
  bool begin(
    const char *deviceName,
    const EspBluedroidClassicSecurityConfig &security);
  void end();
  void update();

  EspBleBluedroid *owner_;
  EspBluedroidClassicInquiry inquiry_;
  EspBluedroidSpp spp_;
  SecurityChangedCallback securityChangedCallback_;
  NumericComparisonCallback numericComparisonCallback_;
  PasskeyDisplayedCallback passkeyDisplayedCallback_;
  PasskeyRequestedCallback passkeyRequestedCallback_;
  EspBluedroidClassicImpl *impl_ = nullptr;
};

class EspBleBluedroid
{
public:
  static constexpr size_t MaxDiscoveredGattServices = 16;
  static constexpr size_t MaxDiscoveredGattCharacteristics = 48;
  static constexpr size_t MaxDiscoveredGattDescriptors = 48;
  using ConnectionCallback =
    std::function<void(const EspBleConnection &connection)>;
  using ConnectionFailureCallback =
    std::function<void(const EspBleConnectionFailure &failure)>;
  using GattResultCallback = std::function<void(const EspBleGattResult &result)>;
  using SecurityChangedCallback =
    std::function<void(const EspBleSecurityChanged &event)>;
  using MtuChangedCallback =
    std::function<void(const EspBleMtuChanged &event)>;
  using PasskeyDisplayedCallback =
    std::function<void(const EspBlePasskeyDisplayed &event)>;

  EspBleBluedroid();
  ~EspBleBluedroid();

  EspBleBluedroid(const EspBleBluedroid &) = delete;
  EspBleBluedroid &operator=(const EspBleBluedroid &) = delete;

  bool begin(const EspBleConfig &config = EspBleConfig());
  void end();
  void update();

  bool initialized() const;
  // The current Public or Random Static address. Returns an empty String before
  // begin() and for controller-managed RPA, whose on-air value is not exposed
  // by the original ESP32 GAP API.
  String localAddress() const;
  EspBleAddressType localAddressType() const;
  // Set the nearest supported BLE radio level (-12..+9 dBm in 3 dB steps on
  // original ESP32). The applied advertising level is returned by txPower().
  bool setTxPower(int8_t dBm);
  int8_t txPower() const;
  EspBluedroidCapabilities capabilities() const;
  EspBleAdvertising &advertising();
  EspBleScanner &scanner();
  EspBluedroidClassic &classic();
#ifdef ESP_BLE_BLUEDROID_TESTING
  bool setSecurityResponseTimeoutForTest(uint32_t timeoutMilliseconds);
  bool injectNotificationForTest(
    const EspBleGattNotification &notification);
  bool injectGattResultForTest(const EspBleGattResult &result);
#endif

  bool connect(
    const EspBleScanResult &scanResult,
    uint32_t timeoutMilliseconds = 10000);
  bool connect(
    const char *address,
    EspBleAddressType addressType,
    uint32_t timeoutMilliseconds = 10000);
  bool disconnect(EspBleConnectionId connectionId);
  // Request new parameters for an active Central connection. Interval values
  // use 1.25 ms units and supervisionTimeout uses 10 ms units. A true return
  // value means that the backend accepted the request; observe the negotiated
  // result through onConnectionParametersUpdated().
  bool updateConnectionParameters(
    EspBleConnectionId connectionId,
    uint16_t minInterval,
    uint16_t maxInterval,
    uint16_t latency,
    uint16_t supervisionTimeout);
  bool discoverServices(
    EspBleConnectionId connectionId,
    uint32_t timeoutMilliseconds = 10000);
  size_t discoveredServiceCount(EspBleConnectionId connectionId) const;
  bool discoveredService(
    EspBleConnectionId connectionId,
    size_t index,
    EspBleGattServiceInfo &service) const;
  size_t discoveredCharacteristicCount(
    EspBleConnectionId connectionId,
    const char *serviceUuid = nullptr) const;
  bool discoveredCharacteristic(
    EspBleConnectionId connectionId,
    size_t index,
    EspBleGattCharacteristicInfo &characteristic,
    const char *serviceUuid = nullptr) const;
  size_t discoveredDescriptorCount(
    EspBleConnectionId connectionId,
    const char *serviceUuid = nullptr,
    const char *characteristicUuid = nullptr) const;
  bool discoveredDescriptor(
    EspBleConnectionId connectionId,
    size_t index,
    EspBleGattDescriptorInfo &descriptor,
    const char *serviceUuid = nullptr,
    const char *characteristicUuid = nullptr) const;
  bool readCharacteristic(
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    uint32_t timeoutMilliseconds = 10000);
  bool writeCharacteristic(
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    const uint8_t *data,
    size_t length,
    bool response = true,
    uint32_t timeoutMilliseconds = 10000);
  bool readDescriptor(
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    const char *descriptorUuid,
    uint32_t timeoutMilliseconds = 10000);
  bool writeDescriptor(
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    const char *descriptorUuid,
    const uint8_t *data,
    size_t length,
    bool response = true,
    uint32_t timeoutMilliseconds = 10000);
  // Handle overloads select one exact characteristic from the discovery
  // snapshot, including characteristics that share a UUID.
  bool readCharacteristic(
    EspBleConnectionId connectionId,
    uint16_t characteristicHandle,
    uint32_t timeoutMilliseconds = 10000);
  bool writeCharacteristic(
    EspBleConnectionId connectionId,
    uint16_t characteristicHandle,
    const uint8_t *data,
    size_t length,
    bool response = true,
    uint32_t timeoutMilliseconds = 10000);
  bool writeCharacteristic(
    EspBleConnectionId connectionId,
    uint16_t characteristicHandle,
    const String &value,
    bool response = true,
    uint32_t timeoutMilliseconds = 10000);
  bool subscribe(
    EspBleConnectionId connectionId,
    uint16_t characteristicHandle,
    bool notifications = true,
    uint32_t timeoutMilliseconds = 10000);
  bool unsubscribe(
    EspBleConnectionId connectionId,
    uint16_t characteristicHandle,
    uint32_t timeoutMilliseconds = 10000);
  bool subscribe(
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    bool notifications = true,
    uint32_t timeoutMilliseconds = 10000);
  bool unsubscribe(
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    uint32_t timeoutMilliseconds = 10000);
  bool writeCharacteristic(
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    const String &value,
    bool response = true,
    uint32_t timeoutMilliseconds = 10000);
  bool writeDescriptor(
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    const char *descriptorUuid,
    const String &value,
    bool response = true,
    uint32_t timeoutMilliseconds = 10000);
  size_t connectionCount() const;
  bool connection(
    EspBleConnectionId connectionId, EspBleConnection &connection) const;
  bool requestSecurity(EspBleConnectionId connectionId);
  bool providePasskey(uint32_t passkey);
  bool confirmNumericComparison(bool accept);
  static constexpr size_t MaxAcceptListEntries = 8;
  bool addToAcceptList(
    const char *address, EspBleAddressType addressType);
  bool removeFromAcceptList(
    const char *address, EspBleAddressType addressType);
  void clearAcceptList();
  size_t acceptListCount() const;
  bool acceptListEntry(size_t index, EspBleBond &entry) const;
  size_t bondCount() const;
  bool bond(size_t index, EspBleBond &bond) const;
  bool deleteBond(const EspBleBond &bond);
  bool deleteAllBonds();
  size_t droppedEventCount() const;

  void onConnected(ConnectionCallback callback);
  void onDisconnected(ConnectionCallback callback);
  void onConnectionFailed(ConnectionFailureCallback callback);
  void onMtuChanged(MtuChangedCallback callback);
  void onConnectionParametersUpdated(ConnectionCallback callback);
  void onSecurityChanged(SecurityChangedCallback callback);
  void onPasskeyDisplayed(PasskeyDisplayedCallback callback);
  void onNumericComparison(PasskeyDisplayedCallback callback);
  void onCharacteristicRead(GattResultCallback callback);
  void onCharacteristicWritten(GattResultCallback callback);
  void onDescriptorRead(GattResultCallback callback);
  void onDescriptorWritten(GattResultCallback callback);
  void onSubscribed(GattResultCallback callback);
  void onUnsubscribed(GattResultCallback callback);
  void onNotification(
    std::function<void(const EspBleGattNotification &notification)> callback);
  void onServicesDiscovered(GattResultCallback callback);

  EspBleError lastError() const;
  const char *lastErrorName() const;
  const String &lastErrorDetail() const;
  void clearError();

private:
  friend class EspBleAdvertising;
  friend class EspBleScanner;
  friend class EspBluedroidClassic;
  friend class EspBluedroidClassicInquiry;
  friend class EspBluedroidSpp;

  void setError(EspBleError error, const char *detail = nullptr);
  bool startGattOperation(
    EspBleGattOperation operation,
    EspBleConnectionId connectionId,
    const char *serviceUuid,
    const char *characteristicUuid,
    const uint8_t *data,
    size_t length,
    bool response,
    const char *descriptorUuid,
    uint32_t timeoutMilliseconds,
    uint16_t characteristicHandle = 0);
  void expireGattOperation();
  void dispatchConnectionEvents();

  EspBleAdvertising advertising_;
  EspBleScanner scanner_;
  EspBluedroidClassic classic_;
  EspBleConnectionImpl *connectionImpl_ = nullptr;
  ConnectionCallback connectedCallback_;
  ConnectionCallback disconnectedCallback_;
  ConnectionFailureCallback connectionFailedCallback_;
  MtuChangedCallback mtuChangedCallback_;
  ConnectionCallback connectionParametersUpdatedCallback_;
  SecurityChangedCallback securityChangedCallback_;
  PasskeyDisplayedCallback passkeyDisplayedCallback_;
  PasskeyDisplayedCallback numericComparisonCallback_;
  GattResultCallback characteristicReadCallback_;
  GattResultCallback characteristicWrittenCallback_;
  GattResultCallback descriptorReadCallback_;
  GattResultCallback descriptorWrittenCallback_;
  GattResultCallback subscribedCallback_;
  GattResultCallback unsubscribedCallback_;
  GattResultCallback servicesDiscoveredCallback_;
  std::function<void(const EspBleGattNotification &notification)>
    notificationCallback_;
  bool initialized_ = false;
  String activeDeviceName_;
  uint16_t activePreferredMtu_ = 247;
  EspBleOwnAddressType activeOwnAddressType_ =
    EspBleOwnAddressType::Public;
  uint8_t activeRandomAddress_[6] = {};
  bool activeRandomAddressPresent_ = false;
  EspBleSecurityConfig activeSecurity_;
  EspBluedroidClassicSecurityConfig activeClassicSecurity_;
  EspBleBond acceptList_[MaxAcceptListEntries];
  size_t acceptListCount_ = 0;
  EspBleError lastError_ = EspBleError::None;
  String lastErrorDetail_;
};

#endif // ESP_BLE_BLUEDROID_H
