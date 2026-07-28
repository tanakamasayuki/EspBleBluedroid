#include <EspBleBluedroid.h>
#include <BLEDevice.h>
#include <BLEServer.h>

static constexpr const char *SERVICE_UUID = "fead";
static constexpr const char *UNREACHABLE_PEER = "02:00:00:00:00:01";

EspBleBluedroid bluetooth;
BLEServer *server = nullptr;

static void restartAdvertising(
  EspBleAdvertisingFilterPolicy policy, const char *label)
{
  bluetooth.advertising().stop();
  bluetooth.advertising().setFilterPolicy(policy);
  if (!bluetooth.advertising().start())
  {
    Serial.printf("ADVERTISE_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }
  Serial.printf("POLICY %s entries=%u\n",
    label, static_cast<unsigned>(bluetooth.acceptListCount()));
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  const bool prebegin = bluetooth.addToAcceptList(
    UNREACHABLE_PEER, EspBleAddressType::Public);
  if (!bluetooth.begin())
  {
    Serial.printf("BEGIN_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }
  Serial.printf("PREBEGIN_REJECTED %u\n", prebegin ? 0 : 1);

  server = BLEDevice::createServer();
  server->createService(SERVICE_UUID)->start();

  if (!bluetooth.addToAcceptList(
        UNREACHABLE_PEER, EspBleAddressType::Public) ||
      !bluetooth.addToAcceptList(
        UNREACHABLE_PEER, EspBleAddressType::Public))
  {
    Serial.printf("ACCEPT_LIST_FAILED %s %s\n",
      bluetooth.lastErrorName(), bluetooth.lastErrorDetail().c_str());
    return;
  }
  EspBleBond entry;
  Serial.printf("ENTRY valid=%u address=%s type=%u\n",
    bluetooth.acceptListEntry(0, entry) ? 1 : 0,
    entry.peerAddress.c_str(),
    static_cast<unsigned>(entry.peerAddressType));

  const bool removed = bluetooth.removeFromAcceptList(
    UNREACHABLE_PEER, EspBleAddressType::Public);
  const bool missingRejected = !bluetooth.removeFromAcceptList(
    UNREACHABLE_PEER, EspBleAddressType::Public);
  const String missingError = bluetooth.lastErrorName();
  const bool restored = bluetooth.addToAcceptList(
    UNREACHABLE_PEER, EspBleAddressType::Public);
  bluetooth.clearAcceptList();
  const size_t clearedCount = bluetooth.acceptListCount();
  const bool readded = bluetooth.addToAcceptList(
    UNREACHABLE_PEER, EspBleAddressType::Public);
  Serial.printf(
    "MUTATION removed=%u missing=%u error=%s restored=%u cleared=%u readded=%u\n",
    removed ? 1 : 0,
    missingRejected ? 1 : 0,
    missingError.c_str(),
    restored ? 1 : 0,
    static_cast<unsigned>(clearedCount),
    readded ? 1 : 0);

  auto &advertising = bluetooth.advertising();
  advertising.setName("Bluedroid Accept List");
  advertising.addServiceUuid(SERVICE_UUID);
  restartAdvertising(
    EspBleAdvertisingFilterPolicy::ConnectionFromAcceptList,
    "restricted");
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'r')
    {
      restartAdvertising(
        EspBleAdvertisingFilterPolicy::ConnectionFromAcceptList,
        "restricted");
    }
    else if (command == 'o')
    {
      restartAdvertising(EspBleAdvertisingFilterPolicy::Any, "open");
    }
    else if (command == '?')
    {
      Serial.printf("ADVERTISING %u\n",
        bluetooth.advertising().isAdvertising() ? 1 : 0);
    }
  }
  bluetooth.update();
  delay(1);
}
