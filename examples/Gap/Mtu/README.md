# Mtu

> 日本語版: [README.ja.md](README.ja.md)

Connects to a Peripheral advertising the Battery Service (`180F`) and observes
the ATT MTU exchange. The desired value is configured through the
`EspBleConfig` passed to `begin()`.

The new ATT link is first reported with MTU 23. Once negotiation completes,
`onMtuChanged()` receives both the previous and negotiated values. The
configured value is only a preference; the smaller limit supported by either
peer wins. `maximumNotificationPayload()` returns the negotiated MTU minus the
three-byte ATT Notification header.

Expected output when the peer supports MTU 185:

```text
Connected with initial MTU 23
MTU changed from 23 to 185 (notification payload up to 182 bytes)
```
