#!/usr/bin/env python3
"""Regenerate the EspBle API snapshot and the parity table.

The parity check in `tests/unit/api_parity` must run with no sibling checkout, so
the EspBle side of the comparison is a committed snapshot of its public symbols.
This tool produces that snapshot from an EspBle header and refreshes
`docs/API_PARITY.tsv`, keeping the reason and note of every row that still
exists.

    python3 tools/gen_api_parity.py --espble-header ../EspBle/src/EspBle.h \
                                    --espble-source ../EspBle/src/EspBle.cpp \
                                    --espble-midi-profile ../EspBle/src/EspBleMidiProfile.h \
                                    --espble-version 1.1.0

The snapshot has three parts: `espble.symbols` (names and shapes, from the header),
`espble.values` (the enum-to-string maps of the `*Name()` functions, from the
implementation) and `espble.midi_profile` (the code lines of the MIDI profile
helper). The second exists because two libraries can agree on every signature and
still return different strings — see tests/unit/api_parity/values.py. The third
exists because that helper is not a public-API comparison at all: it is EspBle's
file with one type renamed, and the test holds it to being exactly that — see
tests/unit/api_parity/ported.py.

Rows the tool cannot classify are written with reason `TODO`, which makes the
test fail until a human decides whether the difference is a backend constraint, a
Classic extension, or missing work.
"""

import argparse
import hashlib
import pathlib
import re
import sys

REPOSITORY = pathlib.Path(__file__).resolve().parents[1]
PARITY_DIR = REPOSITORY / "tests" / "unit" / "api_parity"
sys.path.insert(0, str(PARITY_DIR))

import ported as ported_normalizer  # noqa: E402
import symbols as symbol_extractor  # noqa: E402
import values as value_extractor  # noqa: E402

TABLE = REPOSITORY / "docs" / "API_PARITY.tsv"
SNAPSHOT = PARITY_DIR / "espble.symbols"
VALUE_SNAPSHOT = PARITY_DIR / "espble.values"
MIDI_PROFILE_SNAPSHOT = PARITY_DIR / "espble.midi_profile"
HEADER = REPOSITORY / "src" / "EspBleBluedroid.h"
SOURCE = REPOSITORY / "src" / "EspBleBluedroid.cpp"

AUTO_RULES = (
    (
        "espble_only",
        re.compile(r"^(EspBleHid|EspBleMidi|ESP_BLE_HID)"),
        "planned",
        "HID over GATT / BLE MIDI; docs/PROFILE_BRIDGE_ROADMAP.ja.md Phase 1",
    ),
    (
        "espble_only",
        re.compile(r"^EspBle::hid[A-Z]"),
        "planned",
        "HID over GATT / BLE MIDI; docs/PROFILE_BRIDGE_ROADMAP.ja.md Phase 1",
    ),
    (
        "espble_only",
        re.compile(r"Listener|^EspBleCallbackList|^EspBle(Invalid)?ListenerId"),
        "planned",
        "multi-observer dispatch; tests/TEST_PLAN.md P3. Only the single on*() "
        "primary exists here, which is why the MIDI and HID helpers cannot be "
        "ported unchanged",
    ),
    (
        "espble_only",
        re.compile(r"[Aa]utoReconnect|[Pp]ersistentSubscription"),
        "planned",
        "library-level feature, not a backend limit; "
        "examples/Gatt/Basics/AutoReconnectClient shows the manual pattern",
    ),
    (
        "espble_only",
        re.compile(r"Phy"),
        "backend",
        "Bluetooth 4.2 LE radio: no LE 2M or Coded PHY to select or report",
    ),
    (
        "bluedroid_only",
        re.compile(r"^EspBluedroid"),
        "classic",
        "Bluetooth Classic extension; EspBle is BLE only",
    ),
    (
        "bluedroid_value_only",
        re.compile(r"^lastErrorName/EspBleError::Unsupported$"),
        "classic",
        "error code for a Classic profile the build cannot provide "
        "(src/EspBleBluedroidA2dp.cpp, ...Hfp.cpp, ...Avrcp.cpp); EspBle is BLE "
        "only and has no such state",
    ),
)


def classify(side, symbol):
    for rule_side, pattern, reason, note in AUTO_RULES:
        if side == rule_side and pattern.search(symbol):
            return reason, note
    return "TODO", "classify this difference"


def read_table():
    rows = {}
    if not TABLE.exists():
        return rows
    for line in TABLE.read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) < 4:
            continue
        side, symbol, reason, note = fields[0], fields[1], fields[2], fields[3]
        rows[(side, symbol)] = (reason, note)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--espble-header", required=True)
    parser.add_argument("--espble-source", required=True)
    parser.add_argument("--espble-midi-profile", required=True)
    parser.add_argument("--espble-version", required=True)
    arguments = parser.parse_args()

    espble_header = pathlib.Path(arguments.espble_header)
    espble_text = espble_header.read_text()
    espble = symbol_extractor.extract(espble_text)
    ours = symbol_extractor.extract(HEADER.read_text())

    digest = hashlib.sha256(espble_text.encode()).hexdigest()
    snapshot_lines = [
        "# Public symbols of EspBle's root header, used by tests/unit/api_parity.",
        "# Regenerate with tools/gen_api_parity.py; never edit by hand.",
        "# espble_version\t%s" % arguments.espble_version,
        "# espble_header\t%s" % espble_header.name,
        "# sha256\t%s" % digest,
    ]
    snapshot_lines.extend(sorted(espble))
    SNAPSHOT.write_text("\n".join(snapshot_lines) + "\n")

    espble_source = pathlib.Path(arguments.espble_source)
    espble_source_text = espble_source.read_text()
    espble_values = value_extractor.flatten(
        value_extractor.extract(espble_source_text))
    our_values = value_extractor.flatten(
        value_extractor.extract(SOURCE.read_text()))
    value_digest = hashlib.sha256(espble_source_text.encode()).hexdigest()
    value_lines = [
        "# Enum-to-string maps of EspBle's public *Name() functions, used by",
        "# tests/unit/api_parity. Two libraries can agree on every signature and",
        "# still return different strings, which no header shows.",
        "# Regenerate with tools/gen_api_parity.py; never edit by hand.",
        "# espble_version\t%s" % arguments.espble_version,
        "# espble_source\t%s" % espble_source.name,
        "# sha256\t%s" % value_digest,
    ]
    value_lines.extend(
        "%s\t%s\t%s" % (function, key, value)
        for (function, key), value in sorted(espble_values.items())
    )
    VALUE_SNAPSHOT.write_text("\n".join(value_lines) + "\n")

    midi_profile = pathlib.Path(arguments.espble_midi_profile)
    midi_profile_text = midi_profile.read_text()
    midi_profile_lines = [
        "# Code lines of EspBle's BLE MIDI profile helper, used by",
        "# tests/unit/api_parity. Our copy is the same file with the library",
        "# reference retyped, so comments are dropped and EspBleBluedroid is",
        "# rewritten to EspBle before comparing -- see ported.py.",
        "# Regenerate with tools/gen_api_parity.py; never edit by hand.",
        "# espble_version\t%s" % arguments.espble_version,
        "# espble_midi_profile\t%s" % midi_profile.name,
        "# sha256\t%s" % hashlib.sha256(midi_profile_text.encode()).hexdigest(),
    ]
    midi_profile_lines.extend(ported_normalizer.normalize(midi_profile_text))
    MIDI_PROFILE_SNAPSHOT.write_text("\n".join(midi_profile_lines) + "\n")

    existing = read_table()
    differences = [("espble_only", symbol) for symbol in sorted(espble - ours)]
    differences += [("bluedroid_only", symbol) for symbol in sorted(ours - espble)]
    # Value differences use the same table, so one place accounts for every
    # divergence from EspBle whatever its kind.
    for key in sorted(set(espble_values) | set(our_values)):
        symbol = "%s/%s" % key
        if key not in our_values:
            differences.append(("espble_value_only", symbol))
        elif key not in espble_values:
            differences.append(("bluedroid_value_only", symbol))
        elif espble_values[key] != our_values[key]:
            differences.append(("value_mismatch", symbol))

    lines = [
        "# Classified differences between EspBle's public API and this library's.",
        "# Regenerate the row set with tools/gen_api_parity.py, then fill in every",
        "# reason and note by hand. tests/unit/api_parity fails on any row with",
        "# reason TODO, any unlisted difference, and any listed row that no longer",
        "# differs.",
        "#",
        "# side\tsymbol\treason\tnote",
        "# side   espble_only    | bluedroid_only",
        "# side   espble_value_only | bluedroid_value_only | value_mismatch",
        "#        (a *Name() mapping, as function/EnumConstant)",
        "# reason backend (a Bluedroid or radio constraint)",
        "#        classic (a Bluetooth Classic extension EspBle does not have)",
        "#        planned (not implemented yet; the note says where it is tracked)",
    ]
    todo = 0
    for side, symbol in differences:
        reason, note = existing.get((side, symbol), (None, None))
        if reason is None or reason == "TODO":
            reason, note = classify(side, symbol)
        if reason == "TODO":
            todo += 1
        lines.append("\t".join((side, symbol, reason, note)))
    TABLE.write_text("\n".join(lines) + "\n")

    print("EspBle %s (header %s, source %s)"
          % (arguments.espble_version, digest[:12], value_digest[:12]))
    print("espble symbols: %d, ours: %d" % (len(espble), len(ours)))
    print("espble name-map entries: %d, ours: %d"
          % (len(espble_values), len(our_values)))
    print("differences: %d (%d need classification)" % (len(differences), todo))
    stale = [key for key in existing if key not in set(differences)]
    for side, symbol in sorted(stale):
        print("dropped row (no longer a difference): %s %s" % (side, symbol))


if __name__ == "__main__":
    main()
