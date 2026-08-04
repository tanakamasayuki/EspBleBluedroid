// en: UserDataClient - connect to a User Data Service (0x181C), subscribe to
//     Database Change Increment notifications, read Age, and write a new First
//     Name and Age. Each write bumps the increment, which arrives as a
//     notification.
// ja: UserDataClient - User Data Service（0x181C）へ接続し、Database Change
//     IncrementのNotificationを購読、AgeをRead、新しいFirst NameとAgeをWriteする。
//     書き込むたびにincrementが増え、Notificationとして届く。
#include <EspBleBluedroid.h>

static constexpr const char *USER_DATA_SERVICE_UUID = "181c";
static constexpr const char *AGE_UUID = "2a80";
static constexpr const char *FIRST_NAME_UUID = "2a8a";
static constexpr const char *DB_CHANGE_INCREMENT_UUID = "2a99";

EspBleBluedroid bluetooth;
EspBleConnectionId connectionId = 0;
bool wroteProfile = false;

void setup()
{
  Serial.begin(115200);
  if (!bluetooth.begin())
  {
    Serial.printf("BLE initialization failed: %s\n", bluetooth.lastErrorDetail().c_str());
    return;
  }

  bluetooth.onConnected([](const EspBleConnection &connection) {
    connectionId = connection.id;
    wroteProfile = false;
    bluetooth.subscribe(connection.id, USER_DATA_SERVICE_UUID, DB_CHANGE_INCREMENT_UUID);
  });
  bluetooth.onSubscribed([](const EspBleGattResult &result) {
    if (result.success)
      bluetooth.readCharacteristic(result.connectionId, USER_DATA_SERVICE_UUID, AGE_UUID);
  });
  bluetooth.onCharacteristicRead([](const EspBleGattResult &result) {
    if (result.characteristicUuid.equalsIgnoreCase(AGE_UUID) && result.success && result.value.length() == 1)
    {
      Serial.printf("Age: %u\n", static_cast<uint8_t>(result.value[0]));
      // en: Write a new profile once, right after reading the current Age. Only
      //     one GATT operation may be in flight per connection here, so the two
      //     writes are chained: First Name now, Age from onCharacteristicWritten.
      // ja: 現在のAgeを読んだ直後に、一度だけ新しいprofileをWriteする。1接続につき
      //     同時1操作なので、2つのWriteは連鎖させる。ここでFirst Nameを書き、Ageは
      //     onCharacteristicWritten から書く。
      if (!wroteProfile)
      {
        wroteProfile = true;
        const uint8_t name[3] = {'A', 'd', 'a'};
        bluetooth.writeCharacteristic(result.connectionId, USER_DATA_SERVICE_UUID, FIRST_NAME_UUID, name, sizeof(name), true);
      }
    }
  });
  bluetooth.onCharacteristicWritten([](const EspBleGattResult &result) {
    if (!result.success)
    {
      Serial.printf("Write failed: %s\n", result.detail.c_str());
      return;
    }
    // en: The First Name write completed, so the Age write may start now.
    // ja: First NameのWriteが完了したので、ここでAgeのWriteを開始できる。
    if (result.characteristicUuid.equalsIgnoreCase(FIRST_NAME_UUID))
    {
      const uint8_t age = 42;
      bluetooth.writeCharacteristic(result.connectionId, USER_DATA_SERVICE_UUID, AGE_UUID, &age, sizeof(age), true);
    }
  });
  bluetooth.onNotification([](const EspBleGattNotification &notification) {
    if (!notification.characteristicUuid.equalsIgnoreCase(DB_CHANGE_INCREMENT_UUID) ||
        notification.value.length() != 4)
      return;
    uint32_t increment = 0;
    for (int i = 3; i >= 0; --i)
      increment = (increment << 8) | static_cast<uint8_t>(notification.value[i]);
    Serial.printf("Database Change Increment: %u\n", static_cast<unsigned>(increment));
  });

  bluetooth.scanner().onResult([](const EspBleScanResult &result) {
    if (result.connectable && result.advertisesService(USER_DATA_SERVICE_UUID))
    {
      bluetooth.scanner().stop();
      bluetooth.connect(result);
    }
  });
  bluetooth.scanner().start();
}

void loop()
{
  bluetooth.update();
  delay(1);
}
