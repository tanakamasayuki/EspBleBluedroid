# EspBleBluedroid Examples

> 日本語版: [README.ja.md](README.ja.md)
> Differences from EspBle: [DIFFERENCES_FROM_ESPBLE.md](DIFFERENCES_FROM_ESPBLE.md)

100 examples for the **original ESP32** — the ESP32 SoC with Bluetooth Classic —
built on the Bluedroid backend bundled with Arduino-ESP32.

The BLE examples are ported from the sibling library
[EspBle](https://github.com/tanakamasayuki/EspBle) and use the same API, so a
sketch usually moves across with a rename. Where usage genuinely differs, the
example's own README has a **"Differences from EspBle"** section with the reason
and the porting recipe; the library-wide list lives in
[DIFFERENCES_FROM_ESPBLE.md](DIFFERENCES_FROM_ESPBLE.md).

## The concepts are covered in the guides

| What you want to know | Guide | Matching examples |
|---|---|---|
| BLE: advertising, scanning, connecting, addresses | [BLE guide](../docs/GUIDE_BLE_BASICS.ja.md) (Japanese), GAP | [Gap/](Gap/) |
| BLE: pairing, bonding, authentication methods | same, Security | [Security/](Security/) |
| BLE: services, characteristics, read / write / notify | same, GATT | [Gatt/](Gatt/) |
| Classic: inquiry, SPP, profiles, pairing | [Classic guide](../docs/GUIDE_CLASSIC_BASICS.ja.md) (Japanese) | [Classic/](Classic/) |
| Running both at once | Classic guide, dual mode | [DualMode/](DualMode/) |

Each example's README stands on its own, so starting from a single example
without reading a guide works fine.

## Building

Every example ships a `sketch.yaml` pinned to the verified Arduino-ESP32
version, so no IDE board setup is needed:

```sh
arduino-cli compile --profile esp32 examples/<path>
```

There is one profile, `esp32`, because this library targets the original ESP32
exclusively — it is the only ESP32 family member with Bluetooth Classic.

## Index

### Getting started

| Example | Role | Description |
|---|---|---|
| [CompileSmoke](CompileSmoke/) | — | Minimal build check; touches the common API surface and prints the library version |
| [Classic/ProfileSupport](Classic/ProfileSupport/) | — | Which Classic profiles are usable here, and why — without starting the stack |

### GAP — advertise, scan, connect

| Example | Role | Description |
|---|---|---|
| [Gap/Advertise](Gap/Advertise/) | Peripheral | Connectable legacy advertising with name, service UUID, manufacturer data, channel map |
| [Gap/Scan](Gap/Scan/) | Central | Continuous active scan printing address / RSSI / name |
| [Gap/Connect](Gap/Connect/) | Central | Scan for a service UUID and connect; async connect / disconnect / failure events |
| [Gap/Mtu](Gap/Mtu/) | Central | Preferred-MTU exchange and notification payload limits |
| [Gap/ConnectionParameters](Gap/ConnectionParameters/) | Central | Change interval / latency / timeout on a live link |
| [Gap/Beacon](Gap/Beacon/) | Broadcaster | Non-connectable, non-scannable beacon with manufacturer data and interval control |
| [Gap/IBeacon](Gap/IBeacon/) | Broadcaster | Broadcast an Apple iBeacon (UUID / major / minor / measured power) |
| [Gap/ServiceData](Gap/ServiceData/) | Broadcaster | Broadcast a temperature as Service Data (AD 0x16), with no connection |
| [Gap/ScanResponse](Gap/ScanResponse/) | Peripheral | Split the payload across advertising data and scan response to get past 31 bytes |
| [Gap/AcceptList](Gap/AcceptList/) | Peripheral | Restrict who may connect, and filter scanning, with the Filter Accept List |
| [Gap/DirectedAdvertising](Gap/DirectedAdvertising/) | Peripheral | Directed advertising aimed at one peer; carries no payload |
| [Gap/PrivateAddress](Gap/PrivateAddress/) | Peripheral | Advertise with a random static or resolvable private address |

### GATT — Basics (generic mechanics + serial)

| Example | Role | Description |
|---|---|---|
| [Gatt/Basics/Server](Gatt/Basics/Server/) | Peripheral | Custom service, readable/writable characteristic, descriptor, read-on-demand value |
| [Gatt/Basics/Client](Gatt/Basics/Client/) | Central | Database discovery → read → write → descriptor chain against the Server |
| [Gatt/Basics/NotifyServer](Gatt/Basics/NotifyServer/) | Peripheral | Subscription-gated periodic notifications |
| [Gatt/Basics/SubscribeClient](Gatt/Basics/SubscribeClient/) | Central | Subscribe to NotifyServer and print notifications |
| [Gatt/Basics/AutoReconnectClient](Gatt/Basics/AutoReconnectClient/) | Central | Keeping a link and a subscription alive by hand |
| [Gatt/Basics/IndicateServer](Gatt/Basics/IndicateServer/) | Peripheral | Acknowledged indications with `onSent()` delivery confirmation |
| [Gatt/Basics/IndicateClient](Gatt/Basics/IndicateClient/) | Central | Subscribe to IndicateServer's indications |
| [Gatt/Basics/NusServer](Gatt/Basics/NusServer/) | Peripheral | NUS-compatible RX writes and TX notification echo |
| [Gatt/Basics/NusClient](Gatt/Basics/NusClient/) | Central | NUS-compatible TX subscription and RX Write Without Response |

### GATT — Device, time & management

| Example | Role | Description |
|---|---|---|
| [Gatt/Device/BatteryServer](Gatt/Device/BatteryServer/) | Peripheral | Standard Battery Level reads and notifications |
| [Gatt/Device/BatteryClient](Gatt/Device/BatteryClient/) | Central | Read and subscribe to Battery Level |
| [Gatt/Device/DeviceInfoServer](Gatt/Device/DeviceInfoServer/) | Peripheral | Standard Device Information strings and PnP ID |
| [Gatt/Device/DeviceInfoClient](Gatt/Device/DeviceInfoClient/) | Central | Sequential Device Information reads and PnP ID decoding |
| [Gatt/Device/UserDataServer](Gatt/Device/UserDataServer/) | Peripheral | Read/write Age and First Name, notify Database Change Increment |
| [Gatt/Device/UserDataClient](Gatt/Device/UserDataClient/) | Central | Write Age/First Name and observe Database Change Increment |
| [Gatt/Device/BondManagementServer](Gatt/Device/BondManagementServer/) | Peripheral | Bond Management Feature and Control Point delete-bond op codes |
| [Gatt/Device/BondManagementClient](Gatt/Device/BondManagementClient/) | Central | Read the Feature bit field and write a delete-bond op code |
| [Gatt/Time/CurrentTimeServer](Gatt/Time/CurrentTimeServer/) | Peripheral | Standard 10-byte Current Time reads and notifications |
| [Gatt/Time/CurrentTimeClient](Gatt/Time/CurrentTimeClient/) | Central | Current Time decoding and notification subscription |
| [Gatt/Time/ReferenceTimeUpdateServer](Gatt/Time/ReferenceTimeUpdateServer/) | Peripheral | Time Update Control Point drives a readable Time Update State |
| [Gatt/Time/ReferenceTimeUpdateClient](Gatt/Time/ReferenceTimeUpdateClient/) | Central | Request/cancel a reference update and read the state |

### GATT — Sensors

| Example | Role | Description |
|---|---|---|
| [Gatt/Sensors/EnvironmentalServer](Gatt/Sensors/EnvironmentalServer/) | Peripheral | Standard Temperature, Humidity, and Pressure values |
| [Gatt/Sensors/EnvironmentalClient](Gatt/Sensors/EnvironmentalClient/) | Central | Scaled sensor reads and Temperature notification subscription |

### GATT — Health & body

| Example | Role | Description |
|---|---|---|
| [Gatt/Health/HeartRateServer](Gatt/Health/HeartRateServer/) | Peripheral | Standard Heart Rate Measurement and Body Sensor Location |
| [Gatt/Health/HeartRateClient](Gatt/Health/HeartRateClient/) | Central | Flags-driven Heart Rate Measurement decoding and subscription |
| [Gatt/Health/HealthThermometerServer](Gatt/Health/HealthThermometerServer/) | Peripheral | IEEE-11073 FLOAT Temperature Measurement indications and Temperature Type |
| [Gatt/Health/HealthThermometerClient](Gatt/Health/HealthThermometerClient/) | Central | Temperature Type read and FLOAT indication decoding |
| [Gatt/Health/BloodPressureServer](Gatt/Health/BloodPressureServer/) | Peripheral | IEEE-11073 SFLOAT systolic/diastolic/mean indications and Feature |
| [Gatt/Health/BloodPressureClient](Gatt/Health/BloodPressureClient/) | Central | Feature read and SFLOAT measurement indication decoding |
| [Gatt/Health/WeightScaleServer](Gatt/Health/WeightScaleServer/) | Peripheral | uint16 Weight Measurement indications (0.005 kg) and Feature |
| [Gatt/Health/WeightScaleClient](Gatt/Health/WeightScaleClient/) | Central | Feature read and Weight Measurement indication decoding |
| [Gatt/Health/BodyCompositionServer](Gatt/Health/BodyCompositionServer/) | Peripheral | Body Fat Percentage + optional Weight indications and Feature |
| [Gatt/Health/BodyCompositionClient](Gatt/Health/BodyCompositionClient/) | Central | Feature read and body-fat / weight decoding |
| [Gatt/Health/PulseOximeterServer](Gatt/Health/PulseOximeterServer/) | Peripheral | SFLOAT SpO2 / pulse-rate Spot-Check indications and Features |
| [Gatt/Health/PulseOximeterClient](Gatt/Health/PulseOximeterClient/) | Central | Features read and SpO2 / pulse-rate decoding |
| [Gatt/Health/GlucoseServer](Gatt/Health/GlucoseServer/) | Peripheral | Record Access Control Point: RACP write → Measurement notify → RACP indicate |
| [Gatt/Health/GlucoseClient](Gatt/Health/GlucoseClient/) | Central | RACP report-records request and measurement/response decoding |
| [Gatt/Health/ContinuousGlucoseMonitoringServer](Gatt/Health/ContinuousGlucoseMonitoringServer/) | Peripheral | E2E-CRC-protected CGM Feature and Measurement notifications |
| [Gatt/Health/ContinuousGlucoseMonitoringClient](Gatt/Health/ContinuousGlucoseMonitoringClient/) | Central | E2E-CRC verification and SFLOAT glucose / time-offset decoding |

### GATT — Fitness & cycling

| Example | Role | Description |
|---|---|---|
| [Gatt/Fitness/CyclingSpeedCadenceServer](Gatt/Fitness/CyclingSpeedCadenceServer/) | Peripheral | Multi-field wheel/crank CSC notifications, Feature, Sensor Location |
| [Gatt/Fitness/CyclingSpeedCadenceClient](Gatt/Fitness/CyclingSpeedCadenceClient/) | Central | Sensor Location read and CSC Measurement decoding |
| [Gatt/Fitness/RunningSpeedCadenceServer](Gatt/Fitness/RunningSpeedCadenceServer/) | Peripheral | Speed/cadence/stride/distance RSC notifications, Feature, Sensor Location |
| [Gatt/Fitness/RunningSpeedCadenceClient](Gatt/Fitness/RunningSpeedCadenceClient/) | Central | Sensor Location read and RSC Measurement decoding |
| [Gatt/Fitness/CyclingPowerServer](Gatt/Fitness/CyclingPowerServer/) | Peripheral | Signed 16-bit power Cycling Power notifications, Feature, Sensor Location |
| [Gatt/Fitness/CyclingPowerClient](Gatt/Fitness/CyclingPowerClient/) | Central | Sensor Location read and signed power decoding |
| [Gatt/Fitness/FitnessMachineServer](Gatt/Fitness/FitnessMachineServer/) | Peripheral | Fitness Machine (FTMS) Indoor Bike Data notifications and Feature |
| [Gatt/Fitness/FitnessMachineClient](Gatt/Fitness/FitnessMachineClient/) | Central | Feature read and flags-driven Indoor Bike Data decoding |
| [Gatt/Fitness/LocationNavigationServer](Gatt/Fitness/LocationNavigationServer/) | Peripheral | Location and Speed notifications (speed + sint32 lat/lon) and LN Feature |
| [Gatt/Fitness/LocationNavigationClient](Gatt/Fitness/LocationNavigationClient/) | Central | LN Feature read and Location and Speed decoding |

### GATT — Alerts & proximity

| Example | Role | Description |
|---|---|---|
| [Gatt/Alerts/AlertNotificationServer](Gatt/Alerts/AlertNotificationServer/) | Peripheral | Category bitmask read, Control Point writes, New Alert notifications |
| [Gatt/Alerts/AlertNotificationClient](Gatt/Alerts/AlertNotificationClient/) | Central | Control Point "Notify New Alert Immediately" and New Alert decoding |
| [Gatt/Alerts/ImmediateAlertServer](Gatt/Alerts/ImmediateAlertServer/) | Peripheral | Find Me target: Alert Level Write Without Response handling |
| [Gatt/Alerts/ImmediateAlertClient](Gatt/Alerts/ImmediateAlertClient/) | Central | Find Me locator: raise/clear Alert Level |
| [Gatt/Alerts/PhoneAlertStatusServer](Gatt/Alerts/PhoneAlertStatusServer/) | Peripheral | Alert Status / Ringer Setting notify, Ringer Control Point silent mode |
| [Gatt/Alerts/PhoneAlertStatusClient](Gatt/Alerts/PhoneAlertStatusClient/) | Central | Read Alert Status, drive Ringer Control Point, decode Ringer Setting |
| [Gatt/Alerts/ProximityServer](Gatt/Alerts/ProximityServer/) | Peripheral | Proximity Reporter: Link Loss Alert Level + Tx Power (two services) |
| [Gatt/Alerts/ProximityClient](Gatt/Alerts/ProximityClient/) | Central | Proximity Monitor: read Tx Power, arm Link Loss Alert Level |

### Security

| Example | Role | Description |
|---|---|---|
| [Security/](Security/) | — | Which side runs where, and why the server set is asymmetric with EspBle |
| [Security/JustWorksServer](Security/JustWorksServer/) | Peripheral | Encrypted characteristic with Just Works pairing + bonding |
| [Security/JustWorksClient](Security/JustWorksClient/) | Central | Just Works pairing, bond store, encrypted read |
| [Security/StaticPasskeyServer](Security/StaticPasskeyServer/) | Peripheral | MITM-authenticated characteristic with a static passkey (display side) |
| [Security/StaticPasskeyClient](Security/StaticPasskeyClient/) | Central | Passkey input side: `requestSecurity()` and authenticated reads |
| [Security/RuntimePasskeyClient](Security/RuntimePasskeyClient/) | Central | Supply a per-pairing passkey at run time with `providePasskey()` |
| [Security/NumericComparisonClient](Security/NumericComparisonClient/) | Central | Confirm the six digits shown on both sides |

### HID over GATT

| Example | Role | Description |
|---|---|---|
| [Hid/KeyboardDevice](Hid/KeyboardDevice/) | Peripheral | BLE HID keyboard: reports, the host's LED output report, Protocol Mode, battery |
| [Hid/KeyboardNkro](Hid/KeyboardNkro/) | Peripheral | N-key rollover: the whole keyboard state as one 29-byte report |
| [Hid/Mouse](Hid/Mouse/) | Peripheral | Relative pointer with buttons and a wheel; drag as a move with the button held |
| [Hid/ConsumerControl](Hid/ConsumerControl/) | Peripheral | Media keys: one 16-bit Consumer page usage per report |
| [Hid/CompositeKeyboardMouse](Hid/CompositeKeyboardMouse/) | Peripheral | Keyboard and mouse in one HID service, told apart by Report ID |
| [Hid/VendorDevice](Hid/VendorDevice/) | Peripheral | Vendor-defined Input, Output and Feature reports of a chosen size |
| [Hid/CustomDevice](Hid/CustomDevice/) | Peripheral | An arbitrary Report Descriptor with caller-declared reports |

### BLE MIDI

| Example | Role | Description |
|---|---|---|
| [Midi/MidiDevice](Midi/MidiDevice/) | Peripheral | BLE MIDI instrument: notes, control change, SysEx, and MIDI received from the host |
| [Midi/MidiHost](Midi/MidiHost/) | Central | BLE MIDI host: discover, subscribe, decode incoming MIDI, and send notes |

### Bluetooth Classic — only on this library

| Example | Role | Description |
|---|---|---|
| [Classic/Inquiry](Classic/Inquiry/) | — | Discover Classic devices: address, name, RSSI, Class of Device |
| [Classic/SppServer](Classic/SppServer/) | SPP server | Unauthenticated SPP server that echoes each packet |
| [Classic/SppClient](Classic/SppClient/) | SPP client | Connect to an SPP server by address and exchange data |
| [Classic/SppSerialServer](Classic/SppSerialServer/) | SPP server | SPP as an Arduino `Stream`, bridged to `Serial` |
| [Classic/SppSerialClient](Classic/SppSerialClient/) | SPP client | The outgoing half of the same `Stream` bridge |
| [Classic/SppSecurity](Classic/SppSecurity/) | SPP server | Authenticated + encrypted SPP with SSP Numeric Comparison |
| [Classic/SppPasskey](Classic/SppPasskey/) | SPP server | Authenticated + encrypted SPP with SSP Passkey Entry |
| [Classic/A2dpSink](Classic/A2dpSink/) | A2DP sink | Receive music as PCM, plus an AVRCP Controller |
| [Classic/A2dpSource](Classic/A2dpSource/) | A2DP source | Send PCM to a speaker, plus an AVRCP Target |
| [Classic/HfpHandsFree](Classic/HfpHandsFree/) | HFP HF | Headset role: SLC, SCO, bidirectional mono PCM |
| [Classic/HfpAudioGateway](Classic/HfpAudioGateway/) | HFP AG | Phone role: accept a headset and carry call audio |
| [Classic/ProfileSupport](Classic/ProfileSupport/) | — | Per-profile availability and the reason behind it |

### Dual mode

| Example | Role | Description |
|---|---|---|
| [DualMode/ScanWhileSpp](DualMode/ScanWhileSpp/) | Both | BLE scanning while a Classic SPP session stays connected |

### Diagnostics

| Example | Role | Description |
|---|---|---|
| [Info/ScanDump](Info/ScanDump/) | Diagnostics | Dump every advertisement field (UUIDs, manufacturer data, …) |
| [Info/ConnectionInspector](Info/ConnectionInspector/) | Diagnostics | Interactively connect and dump MTU, security state, bonds, counters |

## Suggested pairings on two boards

- Gap/Advertise ↔ Gap/Scan
- Gatt/Basics/Server ↔ Gatt/Basics/Client
- Gatt/Basics/NotifyServer ↔ Gatt/Basics/SubscribeClient / AutoReconnectClient (and Gap/Mtu)
- Gatt/Basics/IndicateServer ↔ Gatt/Basics/IndicateClient
- Gatt/Basics/NusServer ↔ Gatt/Basics/NusClient
- Each `Gatt/<Category>/<Name>Server` ↔ its `…Client` (Device, Time, Sensors, Health, Fitness, Alerts)
- Security/JustWorksServer ↔ Security/JustWorksClient
- Security/StaticPasskeyServer ↔ Security/StaticPasskeyClient
- Security/RuntimePasskeyClient and NumericComparisonClient ↔ a phone, an EspBle board, or a raw ESP-IDF peer ([Security/README.md](Security/README.md))
- Each `Hid/*` device ↔ a PC, phone or tablet (pair from the OS Bluetooth settings); Hid/VendorDevice and Hid/CustomDevice need a host that writes their reports
- Midi/MidiDevice ↔ Midi/MidiHost (or a phone/tablet DAW, or a commercial BLE MIDI instrument)
- Classic/SppServer ↔ Classic/SppClient, Classic/SppSerialServer ↔ Classic/SppSerialClient
- Classic/A2dpSource ↔ Classic/A2dpSink, Classic/HfpAudioGateway ↔ Classic/HfpHandsFree
- Classic/SppServer ↔ DualMode/ScanWhileSpp
- Info/ScanDump and Info/ConnectionInspector can observe anything — the other examples, phones, or commercial BLE devices
