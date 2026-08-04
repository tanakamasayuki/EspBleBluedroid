# DirectedAdvertising

> 日本語版: [README.ja.md](README.ja.md)
> Concepts: [BLE communication beginner guide (Japanese)](../../../docs/GUIDE_BLE_BASICS.ja.md) — chapter 2, "GAP"
> EspBle differences: [DIFFERENCES_FROM_ESPBLE.md](../../DIFFERENCES_FROM_ESPBLE.md)

A peripheral-side example that **advertises to exactly one peer**.

Where ordinary advertising broadcasts "anyone may connect", **directed advertising** names the target address in the PDU, so **only that peer may connect**. Its main use is reconnecting quickly to a bonded peer.

Its defining property is that it **cannot carry a payload at all**. By specification, a directed advertising PDU carries only two addresses: the sender's and the target's. No name, no service UUID. The peer therefore does not scan for this device — it **connects by address**.

## Hardware

- 1 × original ESP32 running this sketch (peripheral)
- A central that connects — the [Gap/Connect](../Connect/) example on a second board, or a phone app

Replace `TARGET_CENTRAL` in the sketch with **the address of the central to advertise to**, and `TARGET_TYPE` with its address type. That board can report both with `bluetooth.localAddress()` / `bluetooth.localAddressType()`.

## What it does

- Starts **undirected**, so the central can find this device once and learn its address
- Sending `d` switches to **directed** advertising aimed at `TARGET_CENTRAL`. No payload is transmitted
- The central connects **by address** (`bluetooth.connect(address, addressType)`) rather than from a scan result
- Sending `u` returns to undirected advertising. The payload was kept while directed, just not transmitted

## Key APIs

- `bluetooth.advertising().setDirectedTarget(address, addressType, highDuty)` — set the target
- `bluetooth.advertising().clearDirectedTarget()` — return to normal advertising
- `bluetooth.localAddress()` / `bluetooth.localAddressType()` — how each side tells the other what to target

## Notes

- **No payload is sent.** Not the name, not service UUIDs, not manufacturer data. This is the BLE specification, not a library limitation.
- **If the peer uses an RPA (Resolvable Private Address), give its identity address.** Resolution goes through the bond, so the peer **must be bonded first** (see [Gap/PrivateAddress](../PrivateAddress/) and [Security/JustWorksServer](../../Security/JustWorksServer/)).
- **High Duty Cycle (third argument `true`) stops by itself after 1.28 s.** It advertises every 3.75 ms, which reconnects to a known peer as fast as possible, but it cannot run for long. The default `false` follows `setInterval()` and advertises until `stop()`.
- **Advertising stops once a connection is established.** Restart it explicitly — this sketch does so from the `h` / `l` commands, because there is no peripheral-side `onDisconnected()` here.
- If you only want to restrict who may connect and do not need the fast reconnection, ordinary advertising plus [Gap/AcceptList](../AcceptList/) is easier to work with — the peer can still find this device by scanning.

## Differences from EspBle

| | EspBle | EspBleBluedroid |
|---|---|---|
| Example directory | `Gap/DirectedAdvertise` | `Gap/DirectedAdvertising` |
| Peripheral-side connect / disconnect callbacks | delivered, so `onDisconnected()` can restart advertising | **not delivered** — restart advertising from a command or a timer |
| High / Low Duty Cycle | supported | supported |

**Why:** the directory name follows the peer test (`tests/peer/directed_advertising`) that fixes this behaviour on real hardware. The callback difference is the same one as in [Gap/AcceptList](../AcceptList/): `onConnected()` / `onDisconnected()` describe links this device opened with `connect()`, and incoming peripheral links are not yet published as connection snapshots (see [docs/STATUS.ja.md](../../../docs/STATUS.ja.md)).

**How to port:** move the `advertising().start()` call out of `onDisconnected()`. Everything about the directed PDU itself — the empty payload, the target address type, High Duty stopping after 1.28 s — behaves as in EspBle.

## Expected Serial output

```
Advertising as d0:cf:13:58:fd:94. Send 'd' to direct it at aa:bb:cc:dd:ee:ff.
Directed at aa:bb:cc:dd:ee:ff. No payload is sent.
Undirected: anyone may connect.
```
