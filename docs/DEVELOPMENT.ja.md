# 開発方針

## テストファースト

公開動作を実装・変更するときはRed-Green-Refactorを基本とする。

1. 要求する成功条件、失敗、timeout、callback contextをテストで表現する。
2. 追加したテストが未実装の理由で失敗することを確認する（Red）。
3. そのテストを通す最小の公開API/backend実装を追加する（Green）。
4. 全テストを保ったまま所有権、状態、重複コードを整理する（Refactor）。
5. exampleと設計文書を確定した動作へ合わせる。

文書、CI、release toolingだけの変更や、動作を変えない機械的refactorはRedの対象外に
できる。ただし構文検証、build、既存testなど変更に対応する検証は必ず行う。

## テストの配置

```text
tests/unit/   backend非依存のcodec、parser、状態変換（実機不要）
tests/peer/   2台の無印ESP32で接続・切断・通信・Securityを確認
examples/     公開APIのbuild回帰と利用例
```

- backend非依存ロジックはunit testを先に書く。
- BLE/Classic stackのcallback順、接続、resource解放はpeer testを先に書く。
- EspBleと同等とするBLE APIは、可能な範囲で同じscenarioとwire期待値を使う。
- Classic profileは接続側と待受側、双方向data、切断、再接続、認証失敗を含める。
- BLEとClassicのdual-modeは単独機能testと分け、同時trafficを明示的に検証する。
- flakyな順序を固定化せず、仕様上順序保証が必要なイベントだけ順番をassertする。

## 変更単位

1つの機能変更には原則として次を同じ作業単位で含める。

- 失敗するtest
- public headerとbackend実装
- example
- 日本語/英語README
- API設計またはfeature/status文書
- 必要なArduino IDE keyword

大きなprofileは、接続、Discovery、最小data path、Security、stressの順に小さく分割する。

