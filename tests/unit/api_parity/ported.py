"""Normalize a header that is EspBle's file with the library reference retyped.

Most shared headers (`EspBleMidi.h`, `EspBleKeymap.h`, …) are verbatim copies of
EspBle's, so a plain diff already proves they agree. The profile helpers cannot
be: `EspBleMidiDevice` holds a reference to the library object, and that type is
the one thing that must differ. The claim made in `src/EspBleMidiProfile.h` is
therefore narrower — *only* the type name differs — and this module turns a header
into the form in which that claim is checkable:

  * comment lines and blank lines are dropped, because the port documents itself
    and its notes are meant to differ;
  * every remaining line is stripped of leading and trailing space;
  * `EspBleBluedroid` becomes `EspBle`, which is the substitution being claimed.

What is left is the code. If two headers normalize to the same lines, the only
difference between them is comments and that one type name. `espble.midi_profile`
is a committed snapshot of EspBle's side, so the check needs no sibling checkout,
like the rest of this suite.
"""

import re

# The library type is a whole word: `EspBleBluedroidSppSerial` and friends must not
# be rewritten into something that never existed.
_LIBRARY_TYPE = re.compile(r"\bEspBleBluedroid\b")


def normalize(text, rename=False):
    """Return the code lines of `text`, optionally renaming our library type."""
    lines = []
    for line in text.split("\n"):
        stripped = line.strip()
        if not stripped or stripped.startswith("//"):
            continue
        if rename:
            stripped = _LIBRARY_TYPE.sub("EspBle", stripped)
        lines.append(stripped)
    return lines
