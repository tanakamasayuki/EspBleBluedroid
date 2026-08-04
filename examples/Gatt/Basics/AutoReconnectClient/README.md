# AutoReconnectClient

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../../docs/GUIDE_BLE_BASICS.ja.md) (Japanese) — chapter 4, "GATT"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../../DIFFERENCES_FROM_ESPBLE.md)

Connects to the [Gatt/Basics/NotifyServer](../NotifyServer/) example, subscribes, and **keeps the link alive by hand**: it remembers the peer address, reconnects after a back-off when the link drops, and subscribes again on every new link.

EspBleBluedroid has no `setAutoReconnect()` and no persistent subscriptions, so this example is the hand-written replacement — and it is worth reading even on EspBle, because it shows exactly what the automatic version does for you.

## Hardware

- 1 × original ESP32 running this sketch (central / GATT client)
- 1 × original ESP32 running the [Gatt/Basics/NotifyServer](../NotifyServer/) example

## What it does

- Scans for the NotifyServer service UUID and connects to the first match
- Stores `connection.peerAddress` and `peerAddressType` in `onConnected()`
- Subscribes to the counter characteristic **on every link**, not once
- On a disconnect or a failed attempt, schedules a retry `RECONNECT_DELAY_MS` later and reconnects **by address**, without scanning again
- Prints each notification as it arrives

## The three parts of reconnecting by hand

| Part | Why it is needed |
|---|---|
| Remember the address | `connect(address, addressType)` needs no scan, which is what makes a reconnect fast |
| Back off before retrying | The peer usually needs a moment to restart advertising; an immediate retry just fails with a timeout |
| Subscribe again | A CCCD belongs to the connection. A new link starts unsubscribed on both sides, so notifications stay silent until you re-subscribe |

Issue the retry from `loop()`, not from inside `onDisconnected()`. The callback already runs in the `update()` context, and keeping the back-off in one place makes the state easy to follow.

## Key APIs

- `bluetooth.connect(address, addressType, timeoutMilliseconds)` — reconnect from the remembered address
- `EspBleConnection::peerAddress` / `peerAddressType` — what to remember
- `bluetooth.onDisconnected(callback)` / `bluetooth.onConnectionFailed(callback)` — the two ways a link can end up gone
- `bluetooth.subscribe(id, serviceUuid, characteristicUuid, notifications)` / `bluetooth.onSubscribed(callback)`
- `bluetooth.onNotification(callback)`

## Notes

- **A deliberate `disconnect()` looks the same as a dropped link.** Track your own intent (a flag set before calling `disconnect()`) if the sketch should not reconnect afterwards.
- **A peer that uses a rotating RPA cannot be reconnected by remembered address** unless it is bonded, because the address it advertises has changed. Bond it ([Security/JustWorksClient](../../../Security/JustWorksClient/)) or fall back to scanning.
- One central connection exists at a time, so there is never more than one peer to remember here.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Automatic reconnect | `bluetooth.setAutoReconnect(true)` | **not available** — reconnect from `loop()` as shown |
| Subscription restore | `EspBleConfig::persistentSubscriptions` (on by default) | **not available** — subscribe on every link |
| Peers remembered | several (`MaxRediscoverPeers`) | one, by the sketch itself |
| Automatic re-discovery | `setAutoRediscover()` | **not available** |

**Why:** EspBleBluedroid is still moving its GATT client onto the Bluedroid GATTC API directly (see [docs/STATUS.ja.md](../../../../docs/STATUS.ja.md)). Automatic reconnect and subscription restore need reliable, per-handle state across links, and the library will not ship a policy it cannot yet fix with peer tests on real hardware. Doing it in the sketch keeps the timing and the retry policy visible, which is often what an application wants anyway.

**How to port:** replace `setAutoReconnect(true)` with the remembered-address reconnect in `loop()`, and move the `subscribe()` call out of an `if (!subscribed)` guard so it runs for every link. Note that EspBle's subscription restore keys on the UUID, so it cannot restore duplicate-UUID characteristics either — the by-hand version has no such limit.

## Expected Serial output

```
Connected to 5a:b8:1e:0c:2f:71
Subscription active
Notification: 1
Notification: 2
Disconnected - reconnecting shortly.
Connected to 5a:b8:1e:0c:2f:71
Subscription active
Notification: 3
...
```
