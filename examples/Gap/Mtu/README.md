# Mtu

> 日本語版: [README.ja.md](README.ja.md)

Configures a preferred ATT MTU and observes the negotiated value.

## Requirements

- One original ESP32 running this Central sketch
- A Battery Service Peripheral accepting MTU 185 or larger

## Behavior

- Sets `preferredMtu` to 185
- Reports the new ATT link at its initial MTU 23
- Reports the negotiated MTU and Notification payload limit
- Prints the HCI reason on disconnection

## Main APIs

- `EspBleConfig::preferredMtu`
- `EspBleMtuChanged::previousMtu` / `connection.mtu`
- `maximumNotificationPayload()` / `onMtuChanged()`

## Notes

The smaller limit supported by either peer wins.

## Expected Serial output

```text
Connected with initial MTU 23
MTU changed from 23 to 185 (notification payload up to 182 bytes)
```
