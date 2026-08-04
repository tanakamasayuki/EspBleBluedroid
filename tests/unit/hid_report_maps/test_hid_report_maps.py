"""The HID Report Descriptors must be EspBle's bytes, and must mean what they say.

A HID host learns what a device is by parsing its Report Map, so these bytes are a
wire specification: one byte different from EspBle's and the same sketch behaves
differently on the two libraries. The tables are therefore compared against a
snapshot taken from EspBle's source (`espble.hid_maps`, regenerated with
`tools/gen_hid_report_maps.py`), which needs no sibling checkout.

Equality alone is not enough — a descriptor can match EspBle's and still be wrong,
and the patched fields (mouse button count, vendor report size) have to carry the
meaning they claim. That half is `hid_report_maps_test.cpp`, which feeds the
composed map back through the shared parser.
"""

import re
import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SNAPSHOT = HERE / "espble.hid_maps"
TABLES = HERE / ".." / ".." / ".." / "src" / "internal" / \
    "EspBleBluedroidHidReportMaps.h"

# Snapshot name -> the constant holding the same bytes here.
NAMES = {
    "keyboardMap": "KeyboardMap",
    "nkroKeyboardMap": "NkroKeyboardMap",
    "mouseMap": "MouseMap",
    "gamepadMap": "GamepadMap",
    "consumerMap": "ConsumerMap",
    "systemMap": "SystemMap",
    "vendorMap": "VendorMap",
}


def read_snapshot():
    metadata = {}
    maps = {}
    for line in SNAPSHOT.read_text().splitlines():
        if line.startswith("#"):
            fields = line.lstrip("# ").split("\t")
            if len(fields) == 2:
                metadata[fields[0]] = fields[1]
            continue
        if not line.strip():
            continue
        name, values = line.split("\t")
        maps[name] = values
    return metadata, maps


def read_tables():
    """The byte arrays declared in the header, as lowercase hex strings."""
    text = TABLES.read_text()
    tables = {}
    for name in NAMES.values():
        match = re.search(
            r"uint8_t %s\[\] = \{(.*?)\};" % name, text, re.S
        )
        assert match is not None, "%s is not declared in %s" % (name, TABLES.name)
        body = re.sub(r"//[^\n]*", "", match.group(1))
        tables[name] = "".join(
            value.lower() for value in re.findall(r"0x([0-9a-fA-F]{2})", body)
        )
    return tables


def test_snapshot_records_its_provenance():
    metadata, maps = read_snapshot()
    assert metadata.get("espble_version"), "the snapshot must name the EspBle version"
    assert len(metadata.get("sha256", "")) == 64, "the snapshot must pin a sha256"
    assert set(maps) == set(NAMES), "the snapshot must hold every descriptor"


def test_every_report_descriptor_matches_espble_byte_for_byte():
    _, snapshot = read_snapshot()
    tables = read_tables()
    for espble_name, our_name in NAMES.items():
        assert tables[our_name] == snapshot[espble_name], (
            "%s differs from EspBle's %s. A HID host parses these bytes, so port "
            "the change to EspBle as well, or -- if a backend constraint forces "
            "it -- note the reason in the header and regenerate the snapshot with "
            "tools/gen_hid_report_maps.py.\n  espble: %s\n  ours:   %s"
            % (our_name, espble_name, snapshot[espble_name], tables[our_name])
        )


def test_report_maps_parse_back_to_what_they_declare():
    output = HERE / "output"
    output.mkdir(exist_ok=True)
    binary = output / "hid_report_maps_test"
    compile_result = subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-funsigned-char",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(HERE / ".." / ".." / ".." / "src"),
            str(HERE / "hid_report_maps_test.cpp"),
            "-o",
            str(binary),
        ],
        capture_output=True,
        text=True,
    )
    assert compile_result.returncode == 0, compile_result.stderr
    run_result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run_result.returncode == 0, run_result.stdout + run_result.stderr
