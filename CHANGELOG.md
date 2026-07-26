# Changelog / 変更履歴

## Unreleased
- (EN) Initial release
- (JA) 初期リリース
- (EN) Add runtime BLE passkey entry with two-board peer coverage.
- (JA) 実行時BLE passkey入力と2台peerテストを追加。
- (EN) Add LE Secure Connections Numeric Comparison with explicit confirmation.
- (JA) 明示確認を伴うLE Secure Connections Numeric Comparisonを追加。
- (EN) Cancel pending Security input on disconnect/end and cover rejection retry.
- (JA) 切断・終了時のSecurity入力待ち解除と拒否後の再試行を追加。
- (EN) Add deterministic Scan queue capacity, overflow, and flush coverage.
- (JA) Scan queue容量・overflow・flushの決定的テストを追加。
- (EN) Verify exact connection timeout and established-link shutdown semantics.
- (JA) 接続timeout分類と接続成立後の終了semanticsを実機確認。
- (EN) Add deterministic unanswered Passkey and Numeric Comparison timeout coverage.
- (JA) Passkey・Numeric Comparison未回答timeoutの決定的テストを追加。
- (EN) Add the Classic capability snapshot and asynchronous Inquiry facade
  with two-board peer coverage.
- (JA) Classic capability snapshotと非同期Inquiry facadeを2台peerテスト付きで追加。
- (EN) Add binary-safe Classic SPP Server sessions and reconnect coverage.
- (JA) binary-safeなClassic SPP Server sessionと再接続テストを追加。
- (EN) Add asynchronous Classic SPP Client connections using shared sessions.
- (JA) 共通sessionを使う非同期Classic SPP Client接続を追加。
- (EN) Verify active BLE Scan and binary SPP traffic on one dual-mode stack.
- (JA) 1つのdual-mode stack上のactive BLE Scanとbinary SPP trafficを検証。
- (EN) Verify GATT discovery, Characteristic traffic, notifications, and
  sustained SPP round trips on one dual-mode stack.
- (JA) 1つのdual-mode stack上でGATT Discovery・Characteristic通信・Notificationと
  継続的なSPP往復を検証。
- (EN) Add an ordered eight-entry SPP write queue with overflow diagnostics.
- (JA) 順序保証付き8件SPP送信queueとoverflow診断を追加。
- (EN) Add a bounded 2048-byte SPP receive ring with Stream-like reads.
- (JA) Stream風readとoverflow診断を備えた2048 byte固定長SPP受信ringを追加。
- (EN) Add an Arduino Stream/Print wrapper for established SPP sessions.
- (JA) 確立済みSPP session用Arduino Stream/Printラッパーを追加。
- (EN) Add Classic SSP Numeric Comparison and authenticated/encrypted SPP.
- (JA) Classic SSP Numeric Comparisonと認証・暗号化SPPを追加。
- (EN) Add separate Classic bond management, bonded reconnection, and secure
  SPP Client peer coverage.
- (JA) BLEとは分離したClassic bond管理、bond再接続、secure SPP Client実機テストを追加。
- (EN) Add address-scoped Classic DisplayOnly/KeyboardOnly Passkey Entry with
  two-way peer, unanswered-timeout, late-input rejection, bounded shutdown,
  and retry coverage.
- (JA) peer address付きClassic DisplayOnly/KeyboardOnly Passkey Entryと
  双方向・未回答timeout・遅延入力拒否・入力待ち終了・retry実機テストを追加。
- (EN) Move the primary build and hardware-test baseline to Arduino-ESP32 3.3.11.
- (JA) 主build・実機テスト基準をArduino-ESP32 3.3.11へ更新。
