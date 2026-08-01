# HfpAudioGateway

Starts the HFP Audio Gateway role for incoming Hands-Free SLCs and bidirectional
mono 16-bit PCM. Starting the role makes Classic connectable and discoverable.

The stock Arduino-ESP32 build uses the legacy PCM path backed by the Core's
built-in CVSD/mSBC codec. Bridge through a bounded queue in another library.
Call indicators and call-control APIs are still in progress.
