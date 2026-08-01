# HfpHandsFree

Demonstrates an HFP Hands-Free SLC/SCO session and mono 16-bit PCM converted by
the Core's built-in CVSD/mSBC codec. Pass an Audio Gateway's canonical Classic
address to `connect()`.

`onPcmData()` and `onPcmRequested()` run synchronously on the HFP stack task.
Do not retain buffers or block; copy through a bounded queue. Control callbacks
are delivered by `update()`. Call-control APIs are still in progress.
