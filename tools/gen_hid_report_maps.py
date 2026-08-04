#!/usr/bin/env python3
"""Regenerate the EspBle HID Report Descriptor snapshot.

The descriptors a HID device publishes are the wire specification: a host OS
parses them to learn what the device is, so a byte that differs from EspBle's
makes the same sketch behave differently on the two libraries. `tests/unit/
hid_report_maps` compares our tables against this snapshot, which is taken from
EspBle's source so the check needs no sibling checkout — the same arrangement as
`espble.symbols` and `espble.midi_profile`.

    python3 tools/gen_hid_report_maps.py --espble-source ../EspBle/src/EspBle.cpp \
                                         --espble-version 1.1.0

The tables live in EspBle's implementation rather than in a shared header, which
is why they are extracted by name instead of diffed as a file.
"""

import argparse
import hashlib
import pathlib
import re
import sys

REPOSITORY = pathlib.Path(__file__).resolve().parents[1]
SNAPSHOT = REPOSITORY / "tests" / "unit" / "hid_report_maps" / "espble.hid_maps"

# The order is the order the descriptors are concatenated in, which is itself part
# of the wire format.
TABLES = (
    "keyboardMap",
    "nkroKeyboardMap",
    "mouseMap",
    "gamepadMap",
    "consumerMap",
    "systemMap",
    "vendorMap",
)


def extract(source_text, name):
    match = re.search(r"uint8_t %s\[\] = \{(.*?)\};" % name, source_text, re.S)
    if match is None:
        return None
    body = re.sub(r"//[^\n]*", "", match.group(1))
    return [int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", body)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--espble-source", required=True)
    parser.add_argument("--espble-version", required=True)
    arguments = parser.parse_args()

    source = pathlib.Path(arguments.espble_source)
    text = source.read_text()

    lines = [
        "# HID Report Descriptor bytes of EspBle's device profiles, used by",
        "# tests/unit/hid_report_maps. The descriptors are the wire specification a",
        "# host OS parses, so ours must equal them byte for byte.",
        "# Regenerate with tools/gen_hid_report_maps.py; never edit by hand.",
        "# espble_version\t%s" % arguments.espble_version,
        "# espble_source\t%s" % source.name,
        "# sha256\t%s" % hashlib.sha256(text.encode()).hexdigest(),
    ]
    for name in TABLES:
        values = extract(text, name)
        if values is None:
            print("%s not found in %s" % (name, source), file=sys.stderr)
            return 1
        lines.append("%s\t%s" % (name, "".join("%02x" % value for value in values)))
    SNAPSHOT.write_text("\n".join(lines) + "\n")
    print("EspBle %s: %d descriptors" % (arguments.espble_version, len(TABLES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
