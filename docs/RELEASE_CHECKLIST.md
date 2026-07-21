# Release checklist

Before releasing EspBleBluedroid, verify the following. The shared
`.github/workflows/release.yml` and `tools/bump_version.py` files come from the
common release toolkit and must not be edited per project.

## Documentation and metadata

- The READMEs, API design policy, examples, and implementation agree.
- New behavior was developed test-first as described in `docs/DEVELOPMENT.ja.md`.
- `CHANGELOG.md` contains the user-visible changes under `Unreleased`.
- `library.properties`, `keywords.txt`, and generated build matrices are current.

## Automated verification

```sh
cd tests
uv run --env-file .env pytest --clean
```

```sh
set -euo pipefail
for sketch in $(find examples -name sketch.yaml -printf '%h\n' | sort); do
  arduino-cli compile --profile esp32 "$sketch"
done
```

- Compile every example on the original ESP32.
- Refresh the board and core compatibility matrices.
- Repeat peer suites and check for flaky ordering, heap loss, leaked tasks, and
  inconsistent bond/link-key state.
- After Classic support is added, run each profile suite and the BLE/Classic
  dual-mode suite.

## Release

- Check whitespace, links, generated artifacts, and local `.env` exclusion.
- Preview the version bump with `python tools/bump_version.py --preview`.
- Run the release workflow and verify the branch, tag, archive, and release.
- Compile CompileSmoke from the version published through Arduino Library Manager.

