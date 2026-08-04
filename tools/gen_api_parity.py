#!/usr/bin/env python3
"""Regenerate the EspBle API snapshot and the parity table.

The parity check in `tests/unit/api_parity` must run with no sibling checkout, so
the EspBle side of the comparison is a committed snapshot of its public symbols.
This tool produces that snapshot from an EspBle header and refreshes
`docs/API_PARITY.tsv`, keeping the reason and note of every row that still
exists.

    python3 tools/gen_api_parity.py --espble-header ../EspBle/src/EspBle.h \
                                    --espble-version 1.1.0

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

import symbols as symbol_extractor  # noqa: E402

TABLE = REPOSITORY / "docs" / "API_PARITY.tsv"
SNAPSHOT = PARITY_DIR / "espble.symbols"
HEADER = REPOSITORY / "src" / "EspBleBluedroid.h"

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

    existing = read_table()
    differences = [("espble_only", symbol) for symbol in sorted(espble - ours)]
    differences += [("bluedroid_only", symbol) for symbol in sorted(ours - espble)]

    lines = [
        "# Classified differences between EspBle's public API and this library's.",
        "# Regenerate the row set with tools/gen_api_parity.py, then fill in every",
        "# reason and note by hand. tests/unit/api_parity fails on any row with",
        "# reason TODO, any unlisted difference, and any listed row that no longer",
        "# differs.",
        "#",
        "# side\tsymbol\treason\tnote",
        "# side   espble_only    | bluedroid_only",
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

    print("EspBle %s (%s)" % (arguments.espble_version, digest[:12]))
    print("espble symbols: %d, ours: %d" % (len(espble), len(ours)))
    print("differences: %d (%d need classification)" % (len(differences), todo))
    stale = [key for key in existing if key not in set(differences)]
    for side, symbol in sorted(stale):
        print("dropped row (no longer a difference): %s %s" % (side, symbol))


if __name__ == "__main__":
    main()
