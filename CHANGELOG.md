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
- Add Arduino library metadata, CompileSmoke, release automation, and a core
  compatibility matrix for the generic original ESP32 target.
