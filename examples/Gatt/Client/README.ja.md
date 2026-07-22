# GATT Client

> English: [README.md](README.md)

Database Discovery → Descriptor Read/Write → Characteristic Read → Write With Response → Notification購読を
非同期に実行します。Discovery結果はconnection単位の値snapshotへcopyされ、各完了と
binary値は後から`update()`のcallbackへ配送されます。

現在はGATT操作が同時1件のため、この例のように直前の完了callbackから次の要求を
開始します。
