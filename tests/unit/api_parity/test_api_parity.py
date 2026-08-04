"""Fail when the public API drifts from EspBle without a classified reason.

The libraries are meant to be the same API except where the Bluedroid backend
forces a difference, so every difference has to be a decision someone wrote down.
This test compares the public symbols of `src/EspBleBluedroid.h` against the
pinned EspBle snapshot in `espble.symbols` and requires `docs/API_PARITY.tsv` to
account for each difference — no unlisted difference, no listed row that stopped
being one, no unexplained reason.

Names and shapes are not the whole API. Two libraries can agree on every
signature and still return different strings, which is what happened to
`lastErrorName()`: `INVALID_ARGUMENT` in one and `InvalidArgument` in the other,
invisible in both headers, so code that logged or compared it did not port. The
enum-to-string maps of the `*Name()` functions are therefore compared as well,
against `espble.values`.

Regenerate the row set with `tools/gen_api_parity.py` after changing either
public header, then fill in the reason and note by hand.
"""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import symbols as symbol_extractor
import values as value_extractor

REPOSITORY = pathlib.Path(__file__).resolve().parents[3]
HEADER = REPOSITORY / "src" / "EspBleBluedroid.h"
SOURCE = REPOSITORY / "src" / "EspBleBluedroid.cpp"
SNAPSHOT = pathlib.Path(__file__).parent / "espble.symbols"
VALUE_SNAPSHOT = pathlib.Path(__file__).parent / "espble.values"
TABLE = REPOSITORY / "docs" / "API_PARITY.tsv"

VALID_REASONS = {"backend", "classic", "planned"}
SYMBOL_SIDES = ("espble_only", "bluedroid_only")
VALUE_SIDES = ("espble_value_only", "bluedroid_value_only", "value_mismatch")
# A Classic-only difference is by definition something only this library has.
OURS_ONLY_SIDES = ("bluedroid_only", "bluedroid_value_only")


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
        assert side in SYMBOL_SIDES + VALUE_SIDES, (
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
    # Value rows are checked by their own test; this one is about symbols.
    rows = {row: value for row, value in rows.items() if row[0] in SYMBOL_SIDES}

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


def read_value_snapshot():
    metadata = {}
    entries = {}
    for line in VALUE_SNAPSHOT.read_text().splitlines():
        if line.startswith("#"):
            fields = line.lstrip("# ").split("\t")
            if len(fields) == 2:
                metadata[fields[0]] = fields[1]
            continue
        if not line.strip():
            continue
        function, key, value = line.split("\t")
        entries[(function, key)] = value
    return metadata, entries


def value_differences():
    _, espble = read_value_snapshot()
    ours = value_extractor.flatten(value_extractor.extract(SOURCE.read_text()))
    differences = set()
    for key in set(espble) | set(ours):
        symbol = "%s/%s" % key
        if key not in ours:
            differences.add(("espble_value_only", symbol))
        elif key not in espble:
            differences.add(("bluedroid_value_only", symbol))
        elif espble[key] != ours[key]:
            differences.add(("value_mismatch", symbol))
    return differences


def test_value_snapshot_records_its_provenance():
    metadata, entries = read_value_snapshot()
    assert metadata.get("espble_version"), "the snapshot must name the EspBle version"
    assert len(metadata.get("sha256", "")) == 64, "the snapshot must pin a sha256"
    assert entries, "the snapshot has no name-map entries"
    assert value_extractor.DEFAULT_KEY in {key for _, key in entries}, (
        "the fallback return value is part of the contract and must be recorded"
    )


def test_every_name_map_agrees_with_espble():
    """What a *Name() function returns, not just that it exists.

    An entry that differs has to be a decision in docs/API_PARITY.tsv, the same
    as a missing symbol. `value_mismatch` in particular means an application
    reading the string sees something different depending on the library, which is
    a portability break unless the note says why it is unavoidable.
    """
    rows = read_table()
    differences = value_differences()
    listed = {row for row in rows if row[0] in VALUE_SIDES}

    unlisted = sorted(differences - listed)
    assert not unlisted, (
        "these *Name() return values differ from EspBle and are not in "
        "docs/API_PARITY.tsv; run tools/gen_api_parity.py and classify them:\n  "
        + "\n  ".join("%s %s" % row for row in unlisted)
    )

    stale = sorted(listed - differences)
    assert not stale, (
        "these value rows in docs/API_PARITY.tsv are no longer differences; run "
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
        if reason == "classic" and side not in OURS_ONLY_SIDES
    ]
    assert not wrong, "classic is for symbols only this library has:\n  " + "\n  ".join(
        sorted(wrong)
    )
