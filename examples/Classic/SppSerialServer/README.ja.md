# SppSerialServer

> English: [README.md](README.md)
> 概念の説明: [Bluetooth Classic通信の入門ガイド](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) SPPの章
> EspBle: 対応exampleなし — Bluetooth Classic専用（[DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)）

着信したSPP sessionとボードのUSB `Serial`を、`EspBluedroidSppSerial`（SPPをArduinoの`Stream`として見せるwrapper）で橋渡しします。

「無線シリアルケーブル」を作りたいときに欲しくなる形です。Bluetoothから届いたものは`Serial`へ書き、Serial Monitorで打った内容はBluetoothへ送ります。

```cpp
EspBleBluedroid bluetooth;
EspBluedroidSppSerial sppSerial(bluetooth);
```

## 必要なもの

- このsketchを動かす無印ESP32 × 1（SPP server）
- SPP client × 1 — 2台目のボードで[SppSerialClient](../SppSerialClient/)、スマートフォンのターミナルアプリ、またはPCのBluetoothシリアルポート

## 動作

- `EspBleBluedroid Serial`という名前でSPP serverを開始します
- 接続・切断eventを表示します
- 接続中は`sppSerial`から`Serial`へ、`Serial`から`sppSerial`へ、byteをそのまま転送します

## 主なAPI

- `EspBluedroidSppSerial sppSerial(bluetooth)` — `EspBleBluedroid`インスタンスの隣で、グローバルに1つ構築します
- `sppSerial.connected()` / `explicit operator bool()` / `sessionId()`
- `available()` / `peek()` / `read()` / `write()` / `print()` / `println()` / `readBytes()` / `flush()` — 標準の`Stream`・`Print` API
- `availableForWrite()` — 固定長送信queueの残り容量（byte）

## session IDが不要な理由

wrapperは**現在の単一active sessionへ自分で追従します**。stackもsessionも所有しません。したがって、

- 切断で`connected()`がfalseになり、次のsessionが来ると再びtrueになります（もう一方のroleのsessionでも同じです）
- 再接続後は、アプリ側で結び付け直さなくても新しいsessionを対象にします
- writeはSPPの上限に合わせて990 byte単位へ分割されます

守るべき点は1つだけです。**wrapperは、構築元の`EspBleBluedroid`インスタンスより長く生存してはいけません。**

## メモ

- **`bluetooth.update()`は呼び続けてください。** wrapperはstackが埋める受信ringから読みますが、接続・切断eventや送信完了は`update()`を通ります。
- 切断中の`write()`は失敗します。このsketchのように`connected()`で保護してください。
- `sppSerial.read()`と`onData()`のpacket eventを混ぜると同じbyteを二重に消費します。どちらかに寄せてください。
- session IDを明示的に扱うAPI（echo、queue診断、security）は[SppServer](../SppServer/)を参照してください。

## 期待されるSerial出力

```
connected: id=1 peer=20:32:c6:1e:9d:4a
hello from the phone
disconnected: id=1
```
