# Examples

> 日本語版: [README.ja.md](README.ja.md)

| Area | Example | Purpose |
|---|---|---|
| Build | [CompileSmoke](CompileSmoke/README.md) | Build-check the header, Bluedroid backend guard, and version macro |
| GAP | [Advertise](Gap/Advertise/README.md) | Legacy advertising with a local name, service UUID, and manufacturer data |
| GAP | [Scan](Gap/Scan/README.md) | Active scan and value-type results delivered from `update()` |
| GAP | [Connect](Gap/Connect/README.md) | Asynchronous connection from a Scan Result with a stable connection ID |
| GATT | [Read](Gatt/Read/README.md) | Asynchronous Battery Characteristic Read after connection |
| GATT | [Client](Gatt/Client/README.md) | Read, Write, and Notification subscription callback chain |
| Security | [JustWorksClient](Security/JustWorksClient/README.md) | Just Works pairing, bonding, and encrypted reconnection |
| Security | [StaticPasskeyClient](Security/StaticPasskeyClient/README.md) | Static passkey MITM pairing and authenticated connection state |
| Security | [RuntimePasskeyClient](Security/RuntimePasskeyClient/README.md) | KeyboardOnly MITM pairing with a passkey supplied at runtime |
| Security | [NumericComparisonClient](Security/NumericComparisonClient/README.md) | DisplayYesNo MITM pairing with explicit comparison confirmation |
| Classic | [Inquiry](Classic/Inquiry/README.md) | Capability check and Classic discovery with name, Class of Device, and RSSI |
| Classic | [SppServer](Classic/SppServer/README.md) | Binary-safe SPP Server sessions with deferred connect, data, and disconnect callbacks |
| Classic | [SppClient](Classic/SppClient/README.md) | Asynchronous SDP/RFCOMM connection using the shared SPP session API |
| Classic | [SppSerialServer](Classic/SppSerialServer/README.md) | Serial-style bridge that automatically follows the active SPP Server session |
| Classic | [SppSerialClient](Classic/SppSerialClient/README.md) | Serial-style bridge that automatically follows the active SPP Client session |
| Classic | [SppSecurity](Classic/SppSecurity/README.md) | SSP Numeric Comparison with authenticated and encrypted SPP |
| Classic | [SppPasskey](Classic/SppPasskey/README.md) | DisplayOnly/KeyboardOnly Passkey Entry for secure SPP |
| Dual mode | [ScanWhileSpp](DualMode/ScanWhileSpp/README.md) | Active BLE Scan while a Classic SPP session remains connected |

Public features are implemented test-first, then documented with an example.
