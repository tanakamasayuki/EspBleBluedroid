# GATT Client

> English: [README.md](README.md)

Characteristic Read → Write With Response → Notification購読を非同期に実行します。
各methodの戻り値は要求の受理を表し、完了とcopy済みbinary値は後から`update()`の
callbackへ配送されます。

現在はGATT操作が同時1件のため、この例のように直前の完了callbackから次の要求を
開始します。
