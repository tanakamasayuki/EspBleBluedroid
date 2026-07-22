# Changelog

All notable changes to this project are documented in this file.

## Unreleased

- Establish the Bluedroid peer-test environment and API design policy.
- Add stack lifecycle, Legacy Advertising, active/passive Scan, value-type Scan
  Result, grouped Service UUID fields, explicit 31-byte payload-limit errors,
  timed Advertising, and deferred `update()` callback delivery.
- Add asynchronous single-Central connect/disconnect, stable library connection
  IDs, value snapshots, reconnect/failure handling, bounded in-flight shutdown,
  and deferred lifecycle callbacks.
- Add the first public GATT Client operations: asynchronous, binary-safe
  Characteristic Read and Write with connection validation, response-mode
  selection, and deferred result callbacks.
- Add Notification/Indication subscription, copied notification events, and
  asynchronous unsubscribe completion using explicit Bluedroid CCCD handling.
- Add Arduino library metadata, CompileSmoke, release automation, and a core
  compatibility matrix for the generic original ESP32 target.
