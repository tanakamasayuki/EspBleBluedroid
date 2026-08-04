"""Fail when the public API drifts from EspBle without a classified reason.

The libraries are meant to be the same API except where the Bluedroid backend
forces a difference, so every difference has to be a decision someone wrote down.
This test compares the public symbols of `src/EspBleBluedroid.h` against the
pinned EspBle snapshot in `espble.symbols` and requires `docs/API_PARITY.tsv` to
account for each difference — no unlisted difference, no listed row that stopped
being one, no unexplained reason.

Regenerate the row set with `tools/gen_api_parity.py` after changing either
public header, then fill in the reason and note by hand.
"""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import symbols as symbol_extractor

REPOSITORY = pathlib.Path(__file__).resolve().parents[3]
HEADER = REPOSITORY / "src" / "EspBleBluedroid.h"
SNAPSHOT = pathlib.Path(__file__).parent / "espble.symbols"
TABLE = REPOSITORY / "docs" / "API_PARITY.tsv"

VALID_REASONS = {"backend", "classic", "planned"}


def read_snapshot():
    metadata = {}
    names = set()
    for line in SNAPSHOT.read_text().splitlines():
        if line.startswith("#"):
            fields = line.lstrip("# ").split("\t")
            if len(fields) == 2:
                metadata[fields[0]] = fields[1]
            continue
        if line.strip():
            names.add(line.strip())
    return metadata, names


def read_table():
    rows = {}
    for number, line in enumerate(TABLE.read_text().splitlines(), start=1):
        if line.startswith("#") or not line.strip():
            continue
        fields = line.split("\t")
        assert len(fields) == 4, "%s:%d needs 4 tab-separated fields" % (
            TABLE.name,
            number,
        )
        side, symbol, reason, note = fields
        assert side in ("espble_only", "bluedroid_only"), (
            "%s:%d unknown side %r" % (TABLE.name, number, side)
        )
        rows[(side, symbol)] = (reason, note.strip())
    return rows


def test_snapshot_records_its_provenance():
    metadata, names = read_snapshot()
    assert metadata.get("espble_version"), "the snapshot must name the EspBle version"
    assert len(metadata.get("sha256", "")) == 64, "the snapshot must pin a sha256"
    assert len(names) > 100, "the snapshot looks empty: %d symbols" % len(names)


def test_every_difference_from_espble_is_classified():
    _, espble = read_snapshot()
    ours = symbol_extractor.extract(HEADER.read_text())
    rows = read_table()

    differences = {("espble_only", name) for name in espble - ours}
    differences |= {("bluedroid_only", name) for name in ours - espble}

    unlisted = sorted(differences - set(rows))
    assert not unlisted, (
        "these differences are not in docs/API_PARITY.tsv; run "
        "tools/gen_api_parity.py and classify them:\n  "
        + "\n  ".join("%s %s" % row for row in unlisted)
    )

    stale = sorted(set(rows) - differences)
    assert not stale, (
        "these rows in docs/API_PARITY.tsv are no longer differences; run "
        "tools/gen_api_parity.py to drop them:\n  "
        + "\n  ".join("%s %s" % row for row in stale)
    )


def test_every_classification_gives_a_reason():
    rows = read_table()
    bad = []
    for (side, symbol), (reason, note) in sorted(rows.items()):
        if reason not in VALID_REASONS:
            bad.append("%s %s: reason %r is not one of %s" % (
                side, symbol, reason, sorted(VALID_REASONS)))
        elif not note:
            bad.append("%s %s: reason %s needs a note" % (side, symbol, reason))
        elif reason == "planned" and "docs/" not in note and "TEST_PLAN" not in note \
                and "examples/" not in note and "tests/" not in note:
            bad.append(
                "%s %s: a planned difference must point at where it is tracked"
                % (side, symbol)
            )
    assert not bad, "docs/API_PARITY.tsv:\n  " + "\n  ".join(bad)


def test_classic_extensions_are_only_ours():
    """A Classic-only symbol EspBle also has would mean the reason is wrong."""
    rows = read_table()
    wrong = [
        "%s %s" % (side, symbol)
        for (side, symbol), (reason, _) in rows.items()
        if reason == "classic" and side != "bluedroid_only"
    ]
    assert not wrong, "classic is for symbols only this library has:\n  " + "\n  ".join(
        sorted(wrong)
    )
