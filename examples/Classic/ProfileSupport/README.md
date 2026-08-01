# Classic ProfileSupport

Prints the availability and reason for major Bluetooth Classic profiles without
initializing the Bluetooth stack.

`HidDevice` and `HidHost` cover gamepads. The stock Arduino-ESP32 3.3.11 build
reports `CoreDisabled` because `CONFIG_BT_HID_ENABLED` is disabled.

