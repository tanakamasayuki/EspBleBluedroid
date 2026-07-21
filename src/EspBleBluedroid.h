#ifndef ESP_BLE_BLUEDROID_H
#define ESP_BLE_BLUEDROID_H

#include <sdkconfig.h>

#if !defined(CONFIG_BLUEDROID_ENABLED) || defined(CONFIG_NIMBLE_ENABLED)
#error "EspBleBluedroid requires the Bluedroid backend bundled with Arduino-ESP32"
#endif

#include "espblebluedroid_version.h"

#endif // ESP_BLE_BLUEDROID_H

