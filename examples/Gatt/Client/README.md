# GATT Client

> 日本語版: [README.ja.md](README.ja.md)

Runs an asynchronous Characteristic Read → Write With Response → Notification
subscription flow. Each method returns request acceptance; completion and copied
binary values arrive later from `update()` callbacks.

The current implementation allows one GATT operation at a time. Chain the next
request from the preceding completion callback, as shown here.
