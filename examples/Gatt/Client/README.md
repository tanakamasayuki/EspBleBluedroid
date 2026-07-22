# GATT Client

> 日本語版: [README.ja.md](README.ja.md)

Runs asynchronous database Discovery → Characteristic Read → Write With Response
→ Notification subscription. Discovery data is copied into connection-scoped
value snapshots; all completions and binary values arrive from `update()` callbacks.

The current implementation allows one GATT operation at a time. Chain the next
request from the preceding completion callback, as shown here.
