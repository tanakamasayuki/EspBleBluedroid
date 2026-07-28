# PrivateAddress

> 日本語版: [README.ja.md](README.ja.md)

Advertises with a private address instead of exposing the factory public
address. Observe it from another original ESP32 running [Scan](../Scan/) or
[ScanDump](../../Info/ScanDump/).

Set `USE_RESOLVABLE_PRIVATE_ADDRESS` to select between:

- `RandomStatic`: one fixed random identity generated at `begin()`
- `ResolvablePrivate`: a controller-rotated RPA, useful with bonding and IRK
  exchange

Both appear as `EspBleAddressType::Random` to a scanner. `localAddress()`
returns the on-air Random Static address. It returns an empty string for RPA
because the original ESP32 controller generates its current RPA internally and
does not expose that value through its supported GAP API.

Main APIs are `EspBleConfig::ownAddressType`, `localAddress()`, and
`localAddressType()`.
