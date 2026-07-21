# リリースチェックリスト

EspBleBluedroidをリリースする前の確認項目です。`.github/workflows/release.yml`と
`tools/bump_version.py`は共通release toolkit由来のため、個別projectでは編集しません。

## 事前確認

- `README.ja.md` / `README.md`とAPI設計方針が実装済み範囲に一致している。
- `docs/DEVELOPMENT.ja.md`に従い、新規動作へ先にtestが追加されている。
- `examples/`と各READMEが公開APIに一致している。
- `CHANGELOG.md`の`Unreleased`に利用者向け変更が記録されている。

## メタデータ

- `library.properties`のname、version、説明、architectures、includesを確認する。
- `keywords.txt`に実装済みの主要class/APIだけが記載されている。
- 生成済みの`docs/BOARDS.<version>.md`と`docs/COMPATIBILITY.<version>.md`が
  現在のexample集合を反映している。

## 自動テスト

無印ESP32を2台接続し、stale build cacheを避ける必要がある場合は`--clean`を付ける。

```sh
cd tests
uv run --env-file .env pytest --clean
```

全exampleのcompile:

```sh
set -euo pipefail
for sketch in $(find examples -name sketch.yaml -printf '%h\n' | sort); do
  arduino-cli compile --profile esp32 "$sketch"
done
```

- `compile-examples.yml`が全exampleを無印ESP32で通過している。
- `board-matrix.yml` / `core-matrix.yml`を手動実行し、生成文書を更新する。
- release直前にBLE peer suiteを複数回実行する。
- Classic実装後は各profileとdual-mode suiteも複数回実行する。
- 接続反復後のheap低下、task残留、bond/link key storeの不整合がないことを確認する。

## 手動相互運用

実装済みの機能だけを対象に、実施日と相手機器/OS versionを記録する。

- BLE Scan、接続、GATT read/write/notifyをスマートフォンまたはPCで確認する。
- BLE Security/Bond実装後はJust Works、passkey、再接続を確認する。
- Classic SPP実装後は少なくとも2種類の外部SPP実装で双方向通信を確認する。
- Classic HID/A2DP等はprofile追加時に固有項目をこのchecklistへ追加する。
- BLEとClassicを同時利用する公開機能はdual-mode相互運用を確認する。

## 最終確認とリリース

- `git diff --check`とリンク検索を行い、build artifactやlocal `.env`が含まれないことを確認する。
- `python tools/bump_version.py --preview --level patch`でversion変更を確認する。
- release workflowでversion、CHANGELOG、release branch、tag、GitHub releaseを作成する。
- 公開後にArduino Library Managerから取得できるversionとCompileSmokeのbuildを確認する。

