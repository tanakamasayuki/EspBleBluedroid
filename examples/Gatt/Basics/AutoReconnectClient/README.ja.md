# AutoReconnectClient

> English: [README.md](README.md)
> 概念の説明: [BLE通信の入門ガイド](../../../../docs/GUIDE_BLE_BASICS.ja.md) 4章「GATT編 — データをやり取りする」
> EspBleとの違い: [DIFFERENCES_FROM_ESPBLE.ja.md](../../../DIFFERENCES_FROM_ESPBLE.ja.md)

[Gatt/Basics/NotifyServer](../NotifyServer/) exampleへ接続して購読し、**linkの維持を自分で行う**例です。peerのaddressを覚えておき、切断したらback-offを置いて再接続し、新しいlinkごとに購読をやり直します。

EspBleBluedroidには`setAutoReconnect()`も購読の自動復元もないため、このexampleはその手書きの代替です。同時に「自動版が何をやってくれているのか」がそのまま見えるので、EspBle側を使う場合でも読む価値があります。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（Central / GATT Client）
- [Gatt/Basics/NotifyServer](../NotifyServer/) exampleを動かす無印ESP32 × 1

## 動作

- NotifyServerのService UUIDをscanし、最初に見つかった相手へ接続します
- `onConnected()`で`connection.peerAddress`と`peerAddressType`を保存します
- カウンタCharacteristicの購読を、一度だけではなく**linkごとに**行います
- 切断時・接続失敗時は`RECONNECT_DELAY_MS`後に再試行を予約し、scanし直さずに**addressで**再接続します
- 届いたNotificationを表示します

## 手で再接続するときの3点

| 要素 | 必要な理由 |
|---|---|
| addressを覚える | `connect(address, addressType)`はscanが不要。これが再接続の速さの理由 |
| 再試行前に待つ | 相手はAdvertisingを再開するのに少し時間がかかる。即座の再試行はtimeoutで失敗するだけ |
| 購読をやり直す | CCCDは接続に属する。新しいlinkは両側とも購読なしで始まるので、再購読するまでNotificationは来ない |

再試行は`onDisconnected()`の中ではなく`loop()`から出します。callbackはすでに`update()` contextで走っており、back-offを1か所へ寄せた方が状態を追いやすくなります。

## 主なAPI

- `bluetooth.connect(address, addressType, timeoutMilliseconds)` — 記憶したaddressから再接続する
- `EspBleConnection::peerAddress` / `peerAddressType` — 覚えておく値
- `bluetooth.onDisconnected(callback)` / `bluetooth.onConnectionFailed(callback)` — linkが無くなる2つの経路
- `bluetooth.subscribe(id, serviceUuid, characteristicUuid, notifications)` / `bluetooth.onSubscribed(callback)`
- `bluetooth.onNotification(callback)`

## 注意

- **意図した`disconnect()`と、落ちたlinkは区別できません。** 再接続してほしくない場合は、`disconnect()`の前にフラグを立てるなど、自分の意図を持っておいてください。
- **RPAを回転させる相手は、記憶したaddressでは再接続できません。** advertiseしているaddressが変わっているためです。bondingする（[Security/JustWorksClient](../../../Security/JustWorksClient/)）か、scanへ戻してください。
- Central接続は同時に1つなので、ここで覚えるpeerも常に1台だけです。

## EspBleとの違い

| | EspBle | EspBleBluedroid |
|---|---|---|
| 自動再接続 | `bluetooth.setAutoReconnect(true)` | **なし** — 本例のように`loop()`から再接続する |
| 購読の自動復元 | `EspBleConfig::persistentSubscriptions`（既定on） | **なし** — linkごとに購読する |
| 記憶するpeer数 | 複数（`MaxRediscoverPeers`） | 1台（sketch側で保持） |
| Discoveryの自動やり直し | `setAutoRediscover()` | **なし** |

**なぜ違うのか:** EspBleBluedroidはGATT ClientをBluedroidのGATTC APIへ直接移行している途中です（[docs/STATUS.ja.md](../../../../docs/STATUS.ja.md)参照）。自動再接続と購読復元にはlinkを跨いで信頼できるhandle単位の状態が必要で、実機のpeer testで固定できていない方針をライブラリとして出すことはしていません。sketch側に書けばタイミングと再試行方針が見える形になり、アプリケーションとしてはそのほうが都合がよい場面も多くあります。

**移植のしかた:** `setAutoReconnect(true)`を`loop()`での「記憶したaddressへの再接続」に置き換え、`subscribe()`を`if (!subscribed)`の外へ出してlinkごとに実行します。なお、EspBleの購読復元はUUIDを手掛かりにするため同一UUIDのCharacteristicは復元できませんが、手書き版にはその制限はありません。

## 期待されるSerial出力

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
