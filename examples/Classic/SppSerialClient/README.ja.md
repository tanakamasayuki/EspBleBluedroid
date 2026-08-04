# SppSerialClient

> English: [README.md](README.md)
> 概念の説明: [Bluetooth Classic通信の入門ガイド](../../../docs/GUIDE_CLASSIC_BASICS.ja.md) SPPの章
> EspBle: 対応exampleなし — Bluetooth Classic専用（[DIFFERENCES_FROM_ESPBLE.ja.md](../../DIFFERENCES_FROM_ESPBLE.ja.md)）

`Stream`ブリッジの発信側です。Classic addressを入力して接続すると、それ以降に打った内容はシリアルケーブルのようにSPPを通って相手へ届きます。

`EspBluedroidSppSerial` wrapperは両roleで同じものが使えます。違うのは、どちらが接続を開始するかだけです。

## 必要なもの

- このsketchを動かす無印ESP32 × 1（SPP client）
- SPP server × 1 — 2台目のボードで[SppSerialServer](../SppSerialServer/)、または任意のClassicシリアルポート

## 動作

- **切断中は**入力を促し、`Serial`からClassic addressを待ちます
- そのaddressで`classic().spp().connect(address)`を呼びます
- 接続後は`Serial` → SPP、SPP → `Serial`へ転送します
- 切断・接続失敗の際は理由を表示し、再びaddressの入力を求めます

## 構造上のポイント

このsketchは`sppSerial.connected()`で、**入力された行の意味**を切り替えます。

```cpp
if (!sppSerial.connected() && Serial.available() > 0) { /* addressとして扱う */ }
else { /* 送信データとして扱う */ }
```

この切り替えがないと、接続のために打ったaddressがpayloadとして送信されてしまい、逆に送信データがaddressとして解釈されることもあります。

## 主なAPI

- `bluetooth.classic().spp().connect(address)` — 非同期。結果は`onConnected()` / `onConnectionFailed()`で届きます
- `EspBluedroidSppSerial` — `connected()`、`available()`、`read()`、`write()`、`sessionId()`、`availableForWrite()`
- `onDisconnected()` / `onConnectionFailed()` — このsketchが再びaddressを求める場所

## メモ

- **再接続後、wrapperは新しいsessionへ追従します。** 結び付け直しも、古いsession IDの持ち回りも不要です。
- **`connect()`にはaddressが必要で、scan resultではありません。** 探すには[Inquiry](../Inquiry/)を使います。
- 要求が同期的に拒否された場合（addressが不正、既にsessionがactiveなど）は`false`と`lastErrorDetail()`で分かります。相手が応答しない場合はtimeout eventになります。
- sessionは両roleを通じて同時1つなので、着信sessionがactiveな間はこのsketchから発信できません。

## 期待されるSerial出力

```
Enter a Classic address such as 01:23:45:67:89:ab
connected: id=1 peer=d0:cf:13:58:fd:95
hello from the server
disconnected: id=1
Enter the peer Classic address to reconnect
```
