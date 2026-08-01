# Examples

> 日本語版: [README.ja.md](README.ja.md)

| Area | Example | Purpose |
|---|---|---|
| Build | [CompileSmoke](CompileSmoke/README.md) | Build-check the header, Bluedroid backend guard, and version macro |
| GAP | [Advertise](Gap/Advertise/README.md) | Legacy advertising with a local name, service UUID, and manufacturer data |
| GAP | [Beacon](Gap/Beacon/README.md) | Non-connectable Manufacturer Data beacon |
| GAP | [IBeacon](Gap/IBeacon/README.md) | Apple iBeacon using the shared codec |
| GAP | [ScanResponse](Gap/ScanResponse/README.md) | Compose advertising and scan-response payloads independently |
| GAP | [PrivateAddress](Gap/PrivateAddress/README.md) | Advertise with a Random Static address or controller-managed RPA |
| GAP | [AcceptList](Gap/AcceptList/README.md) | Restrict scan and connection requests with the controller Filter Accept List |
| GAP | [DirectedAdvertising](Gap/DirectedAdvertising/README.md) | High/Low Duty Directed Advertising to one known central |
| GAP | [ServiceData](Gap/ServiceData/README.md) | Broadcast binary Service Data without a connection |
| GAP | [Scan](Gap/Scan/README.md) | Active scan and value-type results delivered from `update()` |
| GAP | [Connect](Gap/Connect/README.md) | Asynchronous connection from a Scan Result with a stable connection ID |
| GAP | [ConnectionParameters](Gap/ConnectionParameters/README.md) | Inspect and update interval, latency, and supervision timeout |
| GAP | [Mtu](Gap/Mtu/README.md) | Configure a preferred ATT MTU and observe the negotiated payload limit |
| GATT Basics | [Client](Gatt/Basics/Client/README.md) | Database discovery, Read/Write, Descriptor access, and Notification subscription |
| GATT Basics | [Server](Gatt/Basics/Server/README.md) | Pre-begin Service, Characteristic, and Descriptor registration with read/write callbacks |
| GATT Basics | [NotifyServer](Gatt/Basics/NotifyServer/README.md) | Counter notifications while a client is subscribed through the CCCD |
| GATT Device | [BatteryClient](Gatt/Device/BatteryClient/README.md) | Standard Battery Level Read and Notification subscription |
| Info | [ConnectionInspector](Info/ConnectionInspector/README.md) | Interactive connection, snapshot, bond, and counter diagnostics |
| Info | [ScanDump](Info/ScanDump/README.md) | Print every public field extracted from advertising |
| Security | [JustWorksClient](Security/JustWorksClient/README.md) | Just Works pairing, bonding, and encrypted reconnection |
| Security | [StaticPasskeyClient](Security/StaticPasskeyClient/README.md) | Static passkey MITM pairing and authenticated connection state |
| Security | [RuntimePasskeyClient](Security/RuntimePasskeyClient/README.md) | KeyboardOnly MITM pairing with a passkey supplied at runtime |
| Security | [NumericComparisonClient](Security/NumericComparisonClient/README.md) | DisplayYesNo MITM pairing with explicit comparison confirmation |
| Classic | [Inquiry](Classic/Inquiry/README.md) | Capability check and Classic discovery with name, Class of Device, and RSSI |
| Classic | [ProfileSupport](Classic/ProfileSupport/README.md) | Report each major profile status and the reason for Core limitations before initialization |
| Classic | [A2dpSink](Classic/A2dpSink/README.md) | Receive 16-bit PCM decoded from SBC by the Core through a synchronous callback |
| Classic | [A2dpSource](Classic/A2dpSource/README.md) | Supply synchronous PCM requests for the Core's built-in SBC encoder |
| Classic | [HfpHandsFree](Classic/HfpHandsFree/README.md) | Hands-Free SLC/SCO and bidirectional PCM through the built-in CVSD/mSBC codec |
| Classic | [HfpAudioGateway](Classic/HfpAudioGateway/README.md) | Incoming Audio Gateway SLC/SCO sessions and bidirectional call PCM |
| Classic | [SppServer](Classic/SppServer/README.md) | Binary-safe SPP Server sessions with deferred connect, data, and disconnect callbacks |
| Classic | [SppClient](Classic/SppClient/README.md) | Asynchronous SDP/RFCOMM connection using the shared SPP session API |
| Classic | [SppSerialServer](Classic/SppSerialServer/README.md) | Serial-style bridge that automatically follows the active SPP Server session |
| Classic | [SppSerialClient](Classic/SppSerialClient/README.md) | Serial-style bridge that automatically follows the active SPP Client session |
| Classic | [SppSecurity](Classic/SppSecurity/README.md) | SSP Numeric Comparison with authenticated and encrypted SPP |
| Classic | [SppPasskey](Classic/SppPasskey/README.md) | DisplayOnly/KeyboardOnly Passkey Entry for secure SPP |
| Dual mode | [ScanWhileSpp](DualMode/ScanWhileSpp/README.md) | Active BLE Scan while a Classic SPP session remains connected |

Public features are implemented test-first, then documented with an example.
