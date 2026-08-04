# EspBleBluedroid ドキュメント

- [BLE通信の入門ガイド](GUIDE_BLE_BASICS.ja.md) — Advertising、Scan、接続、
  Security、GATT Client / Server、UUID
- [Bluetooth Classic通信の入門ガイド](GUIDE_CLASSIC_BASICS.ja.md) —
  Inquiry、SPP、Serial/Stream、BLEとの同時利用
- [Bluetooth Classic profile対応表](CLASSIC_PROFILE_SUPPORT.ja.md) —
  SPP、Audio、HID/GamePadなど主要profileの対応可否、build制約、優先度
- [HID・MIDI・Audio profileのAPI整備計画](PROFILE_BRIDGE_ROADMAP.ja.md) —
  EspBle・EspUsbHost・EspUsbDeviceと揃える値型、複数device、Classic Audioの段階計画
- [EspBle（NimBLE）とのBLE差分](BLE_BACKEND_DIFFERENCES.ja.md) —
  共通API、Bluedroid固有の実装差、意図的に一致させない機能
- [API設計方針](API_DESIGN_POLICY.ja.md) — EspBleとの共通部分、Bluedroid差分、
  Bluetooth Classicを追加しても破綻しない境界
- [実装状況](STATUS.ja.md) — 現在の公開API、実機確認済み範囲、既知の制限
- [開発方針](DEVELOPMENT.ja.md) — テストファースト、テスト配置、変更単位
- [BLE直接バックエンド移行計画](BLE_DIRECT_BACKEND_MIGRATION.ja.md) —
  Arduino BLE wrapperを撤去してESP-IDF Bluedroid APIへ直接接続する段階と完了条件
- [リリースチェックリスト](RELEASE_CHECKLIST.ja.md) — リリース前の確認手順
- [Peerテスト](../tests/README.ja.md) — 無印ESP32 2台での実機テスト手順
- [テスト計画](../tests/TEST_PLAN.ja.md) — 層の分け方、カバレッジ、EspBleとのAPI整合、
  EspBle相互接続suite、優先順位
- [EspBle相互接続テスト](../tests/interop/README.ja.md) — 固定したEspBleリリースを
  peerにしたcross-stack試験の手順と規則
