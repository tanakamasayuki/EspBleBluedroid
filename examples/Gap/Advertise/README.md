# Advertise

> 日本語版: [README.ja.md](README.ja.md)

Advertises a local name, Battery Service UUID, and manufacturer data through
the EspBleBluedroid public API. Call `bluetooth.update()` continuously for
duration handling and event delivery.

Service UUIDs of the same width are grouped into one Complete List. `start()`
fails with `InvalidArgument` when the legacy advertising or scan-response
payload would exceed 31 bytes.
